// src/platform/atomic_file.cpp
#include "platform/atomic_file.h"

#include <atomic>
#include <cerrno>
#include <format>
#include <string>

#include "platform/path_utf8.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__linux__)
#  include <sys/syscall.h>
#  if defined(SYS_openat2)
#    include <linux/openat2.h>
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

}  // namespace platform
