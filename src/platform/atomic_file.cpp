// src/platform/atomic_file.cpp
#include "platform/atomic_file.h"

#include <atomic>
#include <cerrno>
#include <format>
#include <string>

#include "platform/path_utf8.h"

#if defined(_WIN32)
#  if !defined(WIN32_LEAN_AND_MEAN)
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <Windows.h>
#  include <fcntl.h>
#  include <io.h>
#else
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#  if defined(__linux__)
#    include <sys/syscall.h>
#    if defined(SYS_openat2)
#      include <linux/openat2.h>
#    endif
#  endif
#endif

namespace platform {

namespace {

// Longest single component we will create (mirrors vault::MAX_NODE_NAME_BYTES;
// this module must not depend on vault/, so the bound is duplicated).
inline constexpr size_t MAX_EXPORT_COMPONENT_BYTES = 255;

// Collision suffixes never exhaust: this bound turns an attacker pre-creating
// every "name (n).ext" permutation into a bounded failure, not a busy loop.
inline constexpr int MAX_COLLISION_ATTEMPTS = 10000;

// A plain single filename usable as one path component. Anything a hostile
// vault name could smuggle past vault::sanitize_node_name (or a buggy direct
// caller) fails closed HERE, before any syscall: separators, ".", "..", NUL and
// over-long names are refused so the atomic create can only ever target one
// true child of `directory`.
[[nodiscard]] bool is_plain_component(std::string_view name) noexcept
{
    if (name.empty() || name.size() > MAX_EXPORT_COMPONENT_BYTES) return false;
    if (name == "." || name == "..") return false;
    if (name.find_first_of("/\\") != std::string_view::npos) return false;
    if (name.contains('\0')) return false;  // NUL truncates the C syscall string
    return true;
}

// Phase 10 suffix naming: "name (n).ext". Callers only pass n >= 1, so the
// extension split of the ORIGINAL name is computed on demand; a lone leading
// dot is not an extension (".hidden") and a trailing dot never survives
// sanitize_node_name but is treated as no extension for symmetry with
// std::filesystem::path::stem/extension.
[[nodiscard]] std::string collided_name(std::string_view original, int n)
{
    std::string_view stem = original;
    std::string_view ext;
    if (const size_t dot = original.rfind('.');
        dot != std::string_view::npos && dot > 0 && original.size() - dot > 1) {
        stem = original.substr(0, dot);
        ext  = original.substr(dot);
    }
    return std::format("{} ({}){}", stem, n, ext);
}

// TEST SEAM (Phase 98): armed once, the next create reports that an attacker
// already claimed the candidate. Atomic so concurrent creators (and a TSan run)
// cannot trip over it; exchange(false) disarms on read so one injection affects
// exactly one create attempt.
[[nodiscard]] std::atomic_bool& create_collision_flag() noexcept
{
    static std::atomic_bool flag{false};
    return flag;
}

}  // namespace

void inject_atomic_create_collision() noexcept
{
    create_collision_flag().store(true);
}

void clear_atomic_create_collision() noexcept
{
    create_collision_flag().store(false);
}

#if !defined(_WIN32)

namespace {

// RAII close for the directory descriptor.
struct FdGuard {
    int fd = -1;
    ~FdGuard()
    {
        if (fd >= 0) ::close(fd);
    }
    FdGuard() = default;
    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;
};

// Atomic exclusive create of `name` as a direct child of `dirfd`, refusing to
// follow any symlink. Prefers openat2 (RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS:
// no component may escape the directory, none may be a link) and falls back to
// openat(...|O_NOFOLLOW|O_EXCL) — equally safe for a single plain component —
// when the kernel or a seccomp policy lacks openat2. Returns the fd, or -1 with
// errno set (EEXIST means the name is taken).
[[nodiscard]] int openat_creat(int dirfd, const std::string& name) noexcept
{
    constexpr mode_t kMode = 0666;  // NOSONAR cpp:S2612 — see rationale at the fallback open
#if defined(__linux__) && defined(SYS_openat2)
    struct open_how how {};
    how.flags   = static_cast<unsigned long long>(O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC
                                                  | O_NOFOLLOW);
    how.mode    = kMode & 0777;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS;
    if (const long rc = ::syscall(SYS_openat2, dirfd, name.c_str(), &how, sizeof(how));
        rc >= 0) {
        return static_cast<int>(rc);
    }
    if (errno != ENOSYS && errno != EINVAL && errno != EPERM) return -1;
    // kernel without openat2 (or the syscall blocked): fall through to openat.
#endif
    // Exported media is the USER'S readable content, deliberately NOT owner-only:
    // Phase 98's contract matches the pre-P98 fopen("wb") 0666&~umask behaviour
    // (the OPPOSITE of the owner-only keyfile/vault rule) — readable by the user
    // and their tools, never a permission escalation beyond the pre-existing
    // export path. Suppressed here and at the open_how match below (cpp:S2612).  // NOSONAR cpp:S2612
    return ::openat(dirfd, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                    kMode);  // NOSONAR cpp:S2612 — readable user content, not secret
}

}  // namespace

std::optional<NewOutputFile>
create_new_file_within(const std::filesystem::path& directory, std::string_view safe_component)
{
    if (!is_plain_component(safe_component)) return std::nullopt;

    // Hold the directory open so a concurrent rename/replace of the NAME
    // `directory` cannot redirect our create: every operation resolves
    // relative to this descriptor. Opening the directory also follows only the
    // user's own choice of directory (symlinks in ITS ancestry are the user's
    // setup, not an attacker's); what we refuse is following a link at or below
    // the point of create.
    FdGuard dir;
    dir.fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir.fd < 0) return std::nullopt;

    // The FIRST attempt runs the injected "attacker claimed it" seam (if armed)
    // too, so the race is exercised from the very first candidate.
    for (int n = 0; n <= MAX_COLLISION_ATTEMPTS; ++n) {
        if (create_collision_flag().exchange(false)) continue;  // -> next suffix

        const std::string name = (n == 0) ? std::string{safe_component}
                                          : collided_name(safe_component, n);
        if (const int fd = openat_creat(dir.fd, name); fd >= 0) {
            std::FILE* fp = ::fdopen(fd, "wb");
            if (!fp) {
                const int saved = errno;
                ::close(fd);
                (void)::unlinkat(dir.fd, name.c_str(), 0);  // discard the stub
                errno = saved;
                return std::nullopt;
            }
            NewOutputFile out;
            out.fp = fp;
            out.display_path = directory / utf8_to_path(name);
            return out;
        }
        if (errno == EEXIST) continue;  // name taken (file, dir, or symlink) -> suffix
        // RESOLVE_NO_SYMLINKS can report a symlink AT the candidate as ELOOP
        // instead of EEXIST (the final component is a plain name here, so ELOOP
        // can only mean "the name is a link we refuse to follow") — treat that
        // as a collision too, mirroring the openat-O_NOFOLLOW fallback.
        if (errno == ELOOP) continue;
        return std::nullopt;  // permission / quota / I/O: fail closed, nothing left behind
    }
    return std::nullopt;  // collision streak exhausted
}

#else  // defined(_WIN32)

namespace {

// Resolve a path to its TRUE location (junctions / mount points resolved), as
// the "\\?\C:\..." or plain extended form GetFinalPathNameByHandleW returns.
// Both the directory and the created file are normalized through the SAME API,
// so the containment prefix comparison below stays valid.
[[nodiscard]] std::wstring final_native_path(const std::filesystem::path& p,
                                             bool as_directory) noexcept
{
    const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    const DWORD flags = as_directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL;
    HANDLE h = ::CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, share, nullptr, OPEN_EXISTING,
                             flags, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::wstring buf(4096, L'\0');
    const DWORD n =
        ::GetFinalPathNameByHandleW(h, buf.data(), static_cast<DWORD>(buf.size()), 0);
    ::CloseHandle(h);
    if (n == 0 || n >= buf.size()) return {};
    buf.resize(n);
    return buf;
}

// True iff `file` (a freshly-created file's final path) lies strictly beneath
// the directory's final path — i.e. no intermediate junction redirected the
// create outside the folder the user picked. A reparse point AT the candidate
// name is impossible here (CREATE_NEW fails on an existing entry), so the
// final-handle comparison is the only reparse check the create needs.
[[nodiscard]] bool final_path_within(const std::wstring& dir_native,
                                     const std::wstring& file_native) noexcept
{
    if (dir_native.size() >= file_native.size()) return false;
    if (file_native.compare(0, dir_native.size(), dir_native) != 0) return false;
    const wchar_t sep = file_native[dir_native.size()];
    return sep == L'\\' || sep == L'/';
}

}  // namespace

std::optional<NewOutputFile>
create_new_file_within(const std::filesystem::path& directory, std::string_view safe_component)
{
    if (!is_plain_component(safe_component)) return std::nullopt;

    const std::wstring dir_native = final_native_path(directory, /*as_directory=*/true);
    if (dir_native.empty()) return std::nullopt;

    for (int n = 0; n <= MAX_COLLISION_ATTEMPTS; ++n) {
        if (create_collision_flag().exchange(false)) continue;  // -> next suffix

        const std::string name = (n == 0) ? std::string{safe_component}
                                          : collided_name(safe_component, n);
        const std::filesystem::path candidate = directory / utf8_to_path(name);

        // CREATE_NEW atomically claims the name; an existing entry (file, dir,
        // symlink, junction) fails with ERROR_FILE_EXISTS and is NEVER followed
        // or truncated.
        HANDLE h = ::CreateFileW(candidate.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                 CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            const DWORD err = ::GetLastError();
            if (err == ERROR_FILE_EXISTS || err == ERROR_ALREADY_EXISTS) continue;
            return std::nullopt;  // permission / quota / I/O / a dir component: fail closed
        }

        // Ask the OS where the handle REALLY points and reject any redirection
        // out of the chosen directory (intermediate junction/mount-point case).
        std::wstring file_native(4096, L'\0');
        const DWORD f =
            ::GetFinalPathNameByHandleW(h, file_native.data(), static_cast<DWORD>(file_native.size()),
                                        FILE_NAME_NORMALIZED);
        if (f == 0 || f >= file_native.size()) {
            ::CloseHandle(h);
            (void)::DeleteFileW(candidate.c_str());
            return std::nullopt;
        }
        file_native.resize(f);
        if (!final_path_within(dir_native, file_native)) {
            ::CloseHandle(h);
            (void)::DeleteFileW(candidate.c_str());
            return std::nullopt;
        }

        const int fd = ::_open_osfhandle(reinterpret_cast<intptr_t>(h), _O_WRONLY | _O_BINARY);
        if (fd == -1) {
            ::CloseHandle(h);
            (void)::DeleteFileW(candidate.c_str());
            return std::nullopt;
        }
        std::FILE* fp = ::_fdopen(fd, "wb");
        if (!fp) {
            ::_close(fd);
            (void)::DeleteFileW(candidate.c_str());
            return std::nullopt;
        }
        NewOutputFile out;
        out.fp = fp;
        out.display_path = candidate;
        return out;
    }
    return std::nullopt;  // collision streak exhausted
}

#endif  // _WIN32

}  // namespace platform