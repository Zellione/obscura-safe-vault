#include "platform/paths.h"

#include <SDL3/SDL.h>

#include <monocypher.h>

#include <atomic>
#include <array>
#include <cerrno>
#include <cstdio>
#include <limits>
#include <new>
#include <stdexcept>

#include "platform/error_log.h"

#if defined(_WIN32)
#  if !defined(WIN32_LEAN_AND_MEAN)
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <Windows.h>
#  include <Aclapi.h>
#  include <fcntl.h>
#  include <io.h>
#else
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

#include "crypto/random.h"
#include "platform/path_utf8.h"

namespace platform {

// 64-bit-safe seek-to-end + tell, mirroring vault::fileutil (src/vault/file_util.h):
// plain fseek/ftell use `long`, which is 32-bit on Windows (LLP64) even in
// 64-bit builds, silently capping readable files at ~2 GiB there. platform/
// doesn't depend on vault/, so the same fix is duplicated locally.
namespace {

[[nodiscard]] bool file_size64(std::FILE* fp, long long& out_size) noexcept
{
#if defined(_WIN32)
    if (_fseeki64(fp, 0, SEEK_END) != 0) return false;
    const long long pos = _ftelli64(fp);
#else
    if (fseeko(fp, 0, SEEK_END) != 0) return false;
    const off_t pos = ftello(fp);
#endif
    if (pos < 0) return false;
    out_size = pos;
    return true;
}

[[nodiscard]] bool seek_to64(std::FILE* fp, long long off) noexcept
{
#if defined(_WIN32)
    return _fseeki64(fp, off, SEEK_SET) == 0;
#else
    return fseeko(fp, static_cast<off_t>(off), SEEK_SET) == 0;
#endif
}

[[nodiscard]] bool sync_file(std::FILE* fp) noexcept
{
#if defined(_WIN32)
    return ::_commit(::_fileno(fp)) == 0;
#else
    return ::fsync(::fileno(fp)) == 0;
#endif
}

#if defined(_WIN32)
// A current-user-only DACL (full control, no inheritance) plus the trustee
// storage it references; the destructor frees both. Shared by open_new_keyfile
// and create_owner_only_file — a vault is the same class of secret as a
// keyfile. Relying on directory inheritance can expose the file in a shared
// folder, so the DACL is explicit.
struct OwnerOnlyAcl {
    PACL acl = nullptr;
    void* trustee = nullptr;
    ~OwnerOnlyAcl() {
        if (acl) ::LocalFree(acl);
        if (trustee) ::LocalFree(trustee);
    }
};

[[nodiscard]] bool build_owner_only_acl(OwnerOnlyAcl& out) noexcept
{
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    DWORD token_size = 0;
    (void)::GetTokenInformation(token, TokenUser, nullptr, 0, &token_size);
    auto* token_user = static_cast<TOKEN_USER*>(::LocalAlloc(LPTR, token_size));
    if (!token_user || !::GetTokenInformation(token, TokenUser, token_user, token_size,
                                              &token_size)) {
        if (token_user) ::LocalFree(token_user);
        ::CloseHandle(token);
        return false;
    }
    ::CloseHandle(token);

    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = GENERIC_ALL;
    access.grfAccessMode = SET_ACCESS;
    access.grfInheritance = NO_INHERITANCE;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_USER;
    access.Trustee.ptstrName = static_cast<LPWSTR>(token_user->User.Sid);
    PACL acl = nullptr;
    if (::SetEntriesInAclW(1, &access, nullptr, &acl) != ERROR_SUCCESS || !acl) {
        if (acl) ::LocalFree(acl);
        ::LocalFree(token_user);
        return false;
    }
    out.acl = acl;
    out.trustee = token_user;
    return true;
}
#endif

// Atomically claim a brand-new keyfile. The exclusive-create operation closes
// the exists/open race; POSIX mode 0600 does not depend on the caller's umask.
[[nodiscard]] std::FILE* open_new_keyfile(const std::filesystem::path& path) noexcept
{
#if defined(_WIN32)
    OwnerOnlyAcl owner_only;
    if (!build_owner_only_acl(owner_only)) return nullptr;
    SECURITY_DESCRIPTOR descriptor{};
    const bool descriptor_ok =
        ::InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) &&
        ::SetSecurityDescriptorDacl(&descriptor, TRUE, owner_only.acl, FALSE);
    SECURITY_ATTRIBUTES attrs{sizeof(attrs), &descriptor, FALSE};
    HANDLE handle = descriptor_ok
        ? ::CreateFileW(path.c_str(), GENERIC_WRITE | DELETE, 0, &attrs, CREATE_NEW,
                        FILE_ATTRIBUTE_NORMAL, nullptr)
        : INVALID_HANDLE_VALUE;
    if (handle == INVALID_HANDLE_VALUE) return nullptr;
    const int fd = ::_open_osfhandle(reinterpret_cast<intptr_t>(handle),
                                     _O_WRONLY | _O_BINARY);
    if (fd == -1) {
        ::CloseHandle(handle);
        return nullptr;
    }
    std::FILE* fp = ::_fdopen(fd, "wb");
    if (!fp) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        (void)::SetFileInformationByHandle(handle, FileDispositionInfo, &disposition,
                                           sizeof(disposition));
        ::_close(fd);
    }
    return fp;
#else
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                          S_IRUSR | S_IWUSR);
    if (fd == -1) return nullptr;
    std::FILE* fp = ::fdopen(fd, "wb");
    if (!fp) {
        (void)::unlink(path.c_str());
        ::close(fd);
    }
    return fp;
#endif
}

// FILE* cannot be pointer-to-const here: _fileno's C API requires FILE* even
// though this helper only changes the underlying file's disposition.
void discard_created_keyfile(std::FILE* fp, const std::filesystem::path& path) noexcept // NOSONAR cpp:S995
{
#if defined(_WIN32)
    (void)path;
    const intptr_t native = ::_get_osfhandle(::_fileno(fp));
    if (native != -1) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        (void)::SetFileInformationByHandle(reinterpret_cast<HANDLE>(native),
                                           FileDispositionInfo, &disposition,
                                           sizeof(disposition));
    }
#else
    (void)fp;
    (void)path;
    // POSIX permits another process to unlink and replace a pathname while our
    // descriptor remains open. Unlinking by name here could therefore delete
    // somebody else's replacement. Leave the owner-only short file in place;
    // refusing to overwrite it is safer and makes the failed write visible.
#endif
}

} // namespace

std::optional<std::filesystem::path> normalize_user_path(std::string_view raw)
{
    if (raw.empty() || raw.size() > MAX_USER_PATH_BYTES) return std::nullopt;
    // An embedded NUL would truncate the string fopen() actually sees, so the
    // path that gets opened would differ from the one we validated.
    if (raw.contains('\0')) return std::nullopt;

    std::filesystem::path p = utf8_to_path(raw);
    p = p.lexically_normal();
    if (p.empty()) return std::nullopt;
    return p;
}

std::optional<std::string> normalize_external_path_utf8(std::string_view raw)
{
    const auto p = normalize_user_path(raw);
    if (!p.has_value()) return std::nullopt;
    return path_to_utf8(*p);
}

std::filesystem::path config_dir()
{
    char* pref = SDL_GetPrefPath("ObscuraSafeVault", "ObscuraSafeVault");
    if (!pref) return {};
    // SDL_GetPrefPath returns a path with a trailing separator ('\' on
    // Windows); remove it for clean comparison.
    std::string s{pref};
    SDL_free(pref);
    if (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    // SDL_GetPrefPath documents UTF-8; the narrow path ctor would re-decode it
    // through the ANSI code page on Windows (wrong dir for a CJK username).
    return utf8_to_path(s);
}

std::filesystem::path default_vault_path()
{
    auto dir = config_dir();
    return dir.empty() ? std::filesystem::path{"vault.osv"} : dir / "vault.osv";
}

std::optional<std::vector<uint8_t>> read_file(const std::filesystem::path& path,
                                              size_t max_bytes)
{
    std::FILE* f = fopen_path(path, "rb");
    if (!f) return std::nullopt;

    // Size first, then one allocation and one read. Keyfiles pass through
    // here: a chunk-growing vector would strew stale copies of key material
    // across freed heap blocks on every reallocation.
    long long size = -1;
    bool ok = file_size64(f, size) && seek_to64(f, 0);

    std::vector<uint8_t> buf;
    if (ok) {
        const auto usize = static_cast<unsigned long long>(size);
        ok = usize <= max_bytes && usize <= std::numeric_limits<size_t>::max();
    }
    try {
        if (ok && size > 0) {
            buf.resize(static_cast<size_t>(size));
            ok = std::fread(buf.data(), 1, buf.size(), f) == buf.size();
        }
    } catch (const std::bad_alloc&) {
        ok = false;
    } catch (const std::length_error&) {
        ok = false;
    }
    std::fclose(f);
    if (!ok) return std::nullopt;
    return buf;
}

std::optional<std::vector<uint8_t>> read_keyfile(const std::filesystem::path& path)
{
    return read_file(path, MAX_KEYFILE_BYTES);
}

bool write_new_keyfile(const std::filesystem::path& path)
{
    std::array<uint8_t, KEYFILE_SIZE> key{};
    if (!crypto::fill_random(key)) return false;

    std::FILE* f = open_new_keyfile(path);
    if (!f) {
        crypto_wipe(key.data(), key.size());
        return false;
    }
    // The key is intentionally persisted to the caller-selected keyfile. The
    // path was normalized by the UI boundary and open_new_keyfile exclusively
    // created it with an owner-only mode/DACL.
    bool ok = std::fwrite(key.data(), 1, key.size(), f) == key.size() && // NOSONAR cpp:S2083
              std::fflush(f) == 0 && sync_file(f);
    if (!ok) discard_created_keyfile(f, path);
    (void)std::fclose(f);  // data was already flushed+synced; no new error remains to report
    crypto_wipe(key.data(), key.size());  // the keyfile IS key material
    return ok;
}

OwnerOnlyCreate create_owner_only_file(const std::filesystem::path& path, std::FILE*& out)
{
    out = nullptr;
#if defined(_WIN32)
    OwnerOnlyAcl owner_only;
    if (!build_owner_only_acl(owner_only)) return OwnerOnlyCreate::Error;
    SECURITY_DESCRIPTOR descriptor{};
    const bool descriptor_ok =
        ::InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) &&
        ::SetSecurityDescriptorDacl(&descriptor, TRUE, owner_only.acl, FALSE);
    SECURITY_ATTRIBUTES attrs{sizeof(attrs), descriptor_ok ? &descriptor : nullptr, FALSE};
    // dwShareMode MUST allow read+write sharing: Vault::create keeps this write
    // handle (fp_) open while it re-opens the same file for read_fp_ and
    // thumb_fp_. FILE_SHARE_NONE (0) would give the first open an exclusive
    // lock and the second open fails with ERROR_SHARING_VIOLATION.
    HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, &attrs, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return ::GetLastError() == ERROR_FILE_EXISTS ? OwnerOnlyCreate::AlreadyExists
                                                     : OwnerOnlyCreate::Error;
    }
    const int fd = ::_open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDWR | _O_BINARY);
    if (fd == -1) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        (void)::SetFileInformationByHandle(handle, FileDispositionInfo, &disposition,
                                           sizeof(disposition));
        ::CloseHandle(handle);
        return OwnerOnlyCreate::Error;
    }
    out = ::_fdopen(fd, "r+b");
    if (!out) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        (void)::SetFileInformationByHandle(
            reinterpret_cast<HANDLE>(::_get_osfhandle(fd)), FileDispositionInfo, &disposition,
            sizeof(disposition));
        ::_close(fd);
        return OwnerOnlyCreate::Error;
    }
    return OwnerOnlyCreate::Ok;
#else
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC,
                          S_IRUSR | S_IWUSR);
    if (fd == -1) {
        if (errno == EEXIST) return OwnerOnlyCreate::AlreadyExists;
        return OwnerOnlyCreate::Error;
    }
    out = ::fdopen(fd, "r+b");
    if (!out) {
        (void)::unlink(path.c_str());
        ::close(fd);
        return OwnerOnlyCreate::Error;
    }
    return OwnerOnlyCreate::Ok;
#endif
}

void ensure_owner_only_file(const std::filesystem::path& path)
{
    bool ok = false;
#if defined(_WIN32)
    if (OwnerOnlyAcl owner_only; build_owner_only_acl(owner_only)) {
        // SetNamedSecurityInfoW returns an ERROR_* code directly (not via
        // GetLastError). Failing needs WRITE_DAC — a vault on a share owned by
        // somebody else can legitimately not be tightened.
        // SetNamedSecurityInfoW takes LPWSTR (non-const) for a read-only name;
        // const_cast is the standard idiom for this Windows API wart.
        ok = ::SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
                                     DACL_SECURITY_INFORMATION, owner_only.acl, nullptr,
                                     nullptr, nullptr) == ERROR_SUCCESS;
    }
#else
    ok = ::chmod(path.c_str(), S_IRUSR | S_IWUSR) == 0;
#endif
    if (!ok) {
        // Warn once per process: a vault on a share the user cannot tighten
        // would otherwise log on every unlock.
        static std::atomic_flag warned;
        if (!warned.test_and_set()) {
            log_error("vault", "could not enforce owner-only permissions on the vault file");
        }
    }
}

std::string last_open_error_str()
{
    std::string s = "errno=" + std::to_string(errno);
#if defined(_WIN32)
    s += " winerror=" + std::to_string(::GetLastError());
#endif
    return s;
}

std::FILE* open_existing_read(const std::filesystem::path& path)
{
#if defined(_WIN32)
    // Same chain as create_owner_only_file so the CRT deny-table (which would
    // reject this open because fp_ was opened via _open_osfhandle) is bypassed.
    // OPEN_EXISTING: read-back must fail (not create) if the file is absent.
    HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return nullptr;
    const int fd = ::_open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDONLY | _O_BINARY);
    if (fd == -1) {
        ::CloseHandle(handle);
        return nullptr;
    }
    return ::_fdopen(fd, "rb");
#else
    return fopen_path(path, "rb");
#endif
}

} // namespace platform
