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

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "crypto/random.h"
#include "platform/path_utf8.h"

namespace platform {

// 64-bit-safe seek-to-end + tell, mirroring vault::fileutil (src/vault/file_util.h).
namespace {

[[nodiscard]] bool file_size64(std::FILE* fp, long long& out_size) noexcept
{
    if (fseeko(fp, 0, SEEK_END) != 0) return false;
    const off_t pos = ftello(fp);
    if (pos < 0) return false;
    out_size = pos;
    return true;
}

[[nodiscard]] bool seek_to64(std::FILE* fp, long long off) noexcept
{
    return fseeko(fp, static_cast<off_t>(off), SEEK_SET) == 0;
}

// Fault-injection for read_keyfile (OSV-AUD-006). Armed, the next read reports
// a short read as the failure it is, so the partial-read path deterministically
// proves the bytes already read are wiped. The inject_sync_failure convention:
// a single cold branch on the read path, disarmed before and after one short
// read.
[[nodiscard]] std::atomic_bool& keyfile_short_read_flag() noexcept
{
    static std::atomic_bool flag{false};
    return flag;
}

[[nodiscard]] bool sync_file(std::FILE* fp) noexcept
{
    return ::fsync(::fileno(fp)) == 0;
}

// Atomically claim a brand-new keyfile. The exclusive-create operation closes
// the exists/open race; POSIX mode 0600 does not depend on the caller's umask.
[[nodiscard]] std::FILE* open_new_keyfile(const std::filesystem::path& path) noexcept
{
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                          S_IRUSR | S_IWUSR);
    if (fd == -1) return nullptr;
    std::FILE* fp = ::fdopen(fd, "wb");
    if (!fp) {
        (void)::unlink(path.c_str());
        ::close(fd);
    }
    return fp;
}

// FILE* cannot be pointer-to-const here: fileno's C API requires FILE* even
// though this helper only changes the underlying file's disposition.
void discard_created_keyfile(std::FILE* fp, const std::filesystem::path& path) noexcept // NOSONAR cpp:S995
{
    (void)fp;
    (void)path;
    // POSIX permits another process to unlink and replace a pathname while our
    // descriptor remains open. Unlinking by name here could therefore delete
    // somebody else's replacement. Leave the owner-only short file in place;
    // refusing to overwrite it is safer and makes the failed write visible.
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
    // SDL_GetPrefPath returns a path with a trailing separator; remove it for
    // clean comparison.
    std::string s{pref};
    SDL_free(pref);
    if (!s.empty() && s.back() == '/') s.pop_back();
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

void inject_keyfile_short_read() noexcept
{
    keyfile_short_read_flag().store(true);
}

void clear_keyfile_short_read() noexcept
{
    keyfile_short_read_flag().store(false);
}

std::optional<crypto::SecureBytes> read_keyfile(const std::filesystem::path& path)
{
    std::FILE* f = fopen_path(path, "rb");
    if (!f) return std::nullopt;

    // Size first, then one allocation and one read — a chunk-growing buffer
    // would strew stale copies of key material across freed heap blocks on
    // every reallocation. The buffer is a SecureBytes, so every failure path
    // (partial read, too-large file, allocation failure) releases the bytes
    // already read into a wiping, mlock'd blob instead of a plain vector
    // (OSV-AUD-006).
    long long size = -1;
    bool ok = file_size64(f, size) && seek_to64(f, 0);
    if (ok) {
        const auto usize = static_cast<unsigned long long>(size);
        ok = usize <= MAX_KEYFILE_BYTES && usize <= std::numeric_limits<size_t>::max();
    }
    crypto::SecureBytes buf;
    if (ok && size > 0) ok = buf.resize(static_cast<size_t>(size));
    if (ok && size > 0) {
        size_t want = buf.size();
        if (keyfile_short_read_flag().load()) {
            keyfile_short_read_flag().store(false);  // armed once
            want = want / 2;                         // deliberately short read
        }
        const size_t got = std::fread(buf.data(), 1, want, f);
        ok = got == (size_t)size;  // a short read is a partial-read failure
    }
    std::fclose(f);
    if (!ok) return std::nullopt;  // buf is wiped on destruction
    return buf;
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
    // created it with owner-only mode.
    bool ok = std::fwrite(key.data(), 1, key.size(), f) == key.size() && // NOSONAR cpp:S2083
              std::fflush(f) == 0 && sync_file(f);
    if (!ok) discard_created_keyfile(f, path);
    (void)std::fclose(f);  // data was already flushed+synced; no new error remains to report
    crypto_wipe(key.data(), key.size());  // the keyfile IS key material
    return ok;
}

OwnerOnlyCreate create_owner_only_file(const std::filesystem::path& path, std::FILE*& out)
{
    using enum OwnerOnlyCreate;
    out = nullptr;
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC,
                          S_IRUSR | S_IWUSR);
    if (fd == -1) {
        if (errno == EEXIST) return AlreadyExists;
        return Error;
    }
    out = ::fdopen(fd, "r+b");
    if (!out) {
        (void)::unlink(path.c_str());
        ::close(fd);
        return Error;
    }
    return Ok;
}

void ensure_owner_only_file(const std::filesystem::path& path)
{
    bool ok = ::chmod(path.c_str(), S_IRUSR | S_IWUSR) == 0;
    if (!ok) {
        // Warn once per process: a vault on a share the user cannot tighten
        // would otherwise log on every unlock.
        static std::atomic_flag warned;
        if (!warned.test_and_set()) {
            log_error("vault", "could not enforce owner-only permissions on the vault file");
        }
    }
}

} // namespace platform
