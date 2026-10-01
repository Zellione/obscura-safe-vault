#include "vault/v3_fs.h"

#include "crypto/random.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <format>
#include <limits>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>
#if defined(SYS_openat2)
#include <linux/openat2.h>
#endif

namespace vault::v3 {
namespace {

struct FaultState {
    std::atomic<int> point{-1};
    std::atomic<unsigned> countdown{0};
};
FaultState& faults()
{
    static FaultState state;
    return state;
}
bool fail(FsFault p) noexcept
{
    auto& f = faults();
    if (f.point.load() != static_cast<int>(std::to_underlying(p))) return false;
    if (unsigned n = f.countdown.load(); n && f.countdown.compare_exchange_strong(n, n - 1))
        return false;
    f.point.store(-1);
    errno = EIO;
    return true;
}

void close_fd(int& fd) noexcept
{
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

int open_root(const std::filesystem::path& path) noexcept
{
    if (fail(FsFault::Open)) return -1;
#if defined(SYS_openat2)
    struct open_how how{};
    how.flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW;
    how.resolve = RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS;
    if (const long rc = ::syscall(SYS_openat2, AT_FDCWD, path.c_str(), &how, sizeof(how)); rc >= 0)
        return static_cast<int>(rc);
    if (errno != ENOSYS && errno != EINVAL && errno != EPERM) return -1;
#endif
    // Conservative fallback: walk every component with O_NOFOLLOW while
    // retaining the parent descriptor. This gives older kernels the same
    // no-symlink guarantee as openat2 rather than protecting only the leaf.
    int current = ::open(path.is_absolute() ? "/" : ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (current < 0) return -1;
    for (const auto& component : path) {
        const std::string name = component.string();
        if (name.empty() || name == "/" || name == ".") continue;
        if (name == "..") {
            ::close(current);
            errno = EXDEV;
            return -1;
        }
        const int next =
            ::openat(current, name.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        ::close(current);
        if (next < 0) return -1;
        current = next;
    }
    return current;
}

int open_child(int dirfd, const char* name, int flags, mode_t mode = 0) noexcept
{
#if defined(SYS_openat2)
    struct open_how how{};
    how.flags = static_cast<uint64_t>(flags | O_CLOEXEC | O_NOFOLLOW);
    how.mode = mode;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS;
    if (const long rc = ::syscall(SYS_openat2, dirfd, name, &how, sizeof(how)); rc >= 0)
        return static_cast<int>(rc);
    if (errno != ENOSYS && errno != EINVAL && errno != EPERM) return -1;
#endif
    return ::openat(dirfd, name, flags | O_CLOEXEC | O_NOFOLLOW, mode);
}

bool owned_mode(int fd, mode_t type, mode_t permissions, bool one_link) noexcept
{
    struct stat st{};
    return ::fstat(fd, &st) == 0 && (st.st_mode & S_IFMT) == type &&
           (st.st_mode & 0777) == permissions && st.st_uid == ::geteuid() &&
           (!one_link || st.st_nlink == 1);
}

bool same_device(int lhs, int rhs) noexcept
{
    struct stat a{};
    struct stat b{};
    return ::fstat(lhs, &a) == 0 && ::fstat(rhs, &b) == 0 && a.st_dev == b.st_dev;
}

bool sync_dir(int fd) noexcept
{
    return !fail(FsFault::DirectorySync) && ::fsync(fd) == 0;
}

bool create_file(int dirfd, const char* name) noexcept
{
    const int fd = open_child(dirfd, name, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return false;
    const bool ok = ::fchmod(fd, 0600) == 0;
    ::close(fd);
    return ok;
}

bool plain_staging_name(std::string_view n) noexcept
{
    return !n.empty() && n.size() <= 64 && n != "." && n != ".." && !n.contains('/') &&
           !n.contains('\\') && !n.contains('\0');
}

char hex_digit(std::byte n) noexcept
{
    return "0123456789abcdef"[std::to_integer<unsigned>(n) & 15U];
}
int hex_value(char c) noexcept
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool owned_staging_name(std::string_view name) noexcept
{
    if (name.size() != 36 || !name.starts_with("tmp-")) return false;
    return std::ranges::all_of(name.substr(4), [](char c) { return hex_value(c) >= 0; });
}

bool ensure_shard(int objects_fd, std::string_view shard, int& shard_fd) noexcept
{
    std::string name{shard};
    if (::mkdirat(objects_fd, name.c_str(), 0700) == 0) {
        if (::fchmodat(objects_fd, name.c_str(), 0700, AT_SYMLINK_NOFOLLOW) != 0) return false;
        if (!sync_dir(objects_fd)) return false;
    } else if (errno != EEXIST)
        return false;
    shard_fd = open_child(objects_fd, name.c_str(), O_RDONLY | O_DIRECTORY);
    return shard_fd >= 0 && owned_mode(shard_fd, S_IFDIR, 0700, false);
}

const dirent* read_next(DIR* directory, bool& read_error) noexcept
{
    errno = 0;
    const dirent* entry = ::readdir(directory);
    read_error = !entry && errno != 0;
    return entry;
}

bool scan_object_shard(int objects_fd, const dirent& shard_entry,
                       std::vector<ObjectEntry>& result) noexcept
{
    const std::string_view shard{shard_entry.d_name};
    if (shard.size() != 2 || hex_value(shard[0]) < 0 || hex_value(shard[1]) < 0) return false;
    const int shard_fd = open_child(objects_fd, shard_entry.d_name, O_RDONLY | O_DIRECTORY);
    if (shard_fd < 0) return false;
    const int entries_fd = open_child(shard_fd, ".", O_RDONLY | O_DIRECTORY);
    DIR* entries = entries_fd >= 0 ? ::fdopendir(entries_fd) : nullptr;
    if (!entries) {
        if (entries_fd >= 0) ::close(entries_fd);
        ::close(shard_fd);
        return false;
    }
    bool read_error = false;
    bool ok = true;
    while (const dirent* entry = read_next(entries, read_error)) {
        const std::string_view name{entry->d_name};
        if (name == "." || name == "..") continue;
        const auto id =
            parse_object_relative_path("objects/" + std::string(shard) + "/" + std::string(name));
        const int file = id ? open_child(shard_fd, entry->d_name, O_RDONLY) : -1;
        struct stat metadata{};
        const bool valid = id && file >= 0 && owned_mode(file, S_IFREG, 0600, true) &&
                           ::fstat(file, &metadata) == 0 && metadata.st_size >= 0;
        if (file >= 0) ::close(file);
        if (!valid) {
            ok = false;
            break;
        }
        result.emplace_back(*id, static_cast<uint64_t>(metadata.st_size),
                            static_cast<int64_t>(metadata.st_mtim.tv_sec));
    }
    ok = ok && !read_error;
    ::closedir(entries);
    ::close(shard_fd);
    return ok;
}

bool scan_objects(int objects_fd, std::vector<ObjectEntry>& result) noexcept
{
    const int scan_fd = open_child(objects_fd, ".", O_RDONLY | O_DIRECTORY);
    if (scan_fd < 0) return false;
    DIR* shards = ::fdopendir(scan_fd);
    if (!shards) {
        ::close(scan_fd);
        return false;
    }
    bool read_error = false;
    bool ok = true;
    while (const dirent* entry = read_next(shards, read_error)) {
        const std::string_view name{entry->d_name};
        if (name == "." || name == "..") continue;
        if (!scan_object_shard(objects_fd, *entry, result)) {
            ok = false;
            break;
        }
    }
    ok = ok && !read_error;
    ::closedir(shards);
    return ok;
}

bool remove_old_staging_entries(int staging, DIR* entries, int64_t cutoff,
                                StagingCleanup& result) noexcept
{
    bool read_error = false;
    while (const dirent* entry = read_next(entries, read_error)) {
        const std::string_view name{entry->d_name};
        if (name == "." || name == "..") continue;
        if (!owned_staging_name(name)) {
            ++result.quarantined_or_foreign;
            continue;
        }
        const int file = open_child(staging, entry->d_name, O_RDONLY);
        struct stat metadata{};
        const bool valid =
            file >= 0 && owned_mode(file, S_IFREG, 0600, true) && ::fstat(file, &metadata) == 0;
        if (file >= 0) ::close(file);
        if (!valid) {
            ++result.quarantined_or_foreign;
            continue;
        }
        if (metadata.st_mtim.tv_sec >= cutoff) {
            ++result.preserved_recent;
            continue;
        }
        if (fail(FsFault::Unlink) || ::unlinkat(staging, entry->d_name, 0) != 0) return false;
        ++result.removed;
    }
    return !read_error && (result.removed == 0 || sync_dir(staging));
}

bool validate_entries(int parent, bool shards, std::string_view expected_shard = {}) noexcept;

std::optional<bool> entry_is_directory(std::string_view name, bool shards,
                                       std::string_view expected_shard) noexcept
{
    if (shards) {
        if (const bool valid =
                name.size() == 2 && hex_value(name[0]) >= 0 && hex_value(name[1]) >= 0;
            !valid)
            return std::nullopt;
        return true;
    }
    if (expected_shard.empty())
        return plain_staging_name(name) ? std::optional{false} : std::nullopt;
    const std::string relative = "objects/" + std::string(expected_shard) + "/" + std::string(name);
    return parse_object_relative_path(relative) ? std::optional{false} : std::nullopt;
}

bool validate_entry(int parent, const dirent& entry, bool shards,
                    std::string_view expected_shard) noexcept
{
    const std::string_view name{entry.d_name};
    const auto directory = entry_is_directory(name, shards, expected_shard);
    if (!directory.has_value()) return false;
    const int child =
        open_child(parent, entry.d_name, *directory ? O_RDONLY | O_DIRECTORY : O_RDONLY);
    const bool metadata_ok = child >= 0 && owned_mode(child, *directory ? S_IFDIR : S_IFREG,
                                                      *directory ? 0700 : 0600, !*directory);
    const bool contents_ok = metadata_ok && (!*directory || validate_entries(child, false, name));
    if (child >= 0) ::close(child);
    return contents_ok;
}

bool validate_entries(int parent, bool shards, std::string_view expected_shard) noexcept
{
    const int scan = open_child(parent, ".", O_RDONLY | O_DIRECTORY);
    if (scan < 0) return false;
    DIR* dir = ::fdopendir(scan);
    if (!dir) {
        ::close(scan);
        return false;
    }
    errno = 0;
    while (const dirent* e = ::readdir(dir)) {
        if (const std::string_view name{e->d_name};
            name != "." && name != ".." && !validate_entry(parent, *e, shards, expected_shard)) {
            ::closedir(dir);
            return false;
        }
    }
    const bool ok = errno == 0;
    ::closedir(dir);
    return ok;
}

bool rename_noreplace(int oldfd, const char* oldname, int newfd, const char* newname) noexcept
{
    if (fail(FsFault::Rename)) return false;
#if defined(SYS_renameat2)
    return ::syscall(SYS_renameat2, oldfd, oldname, newfd, newname, RENAME_NOREPLACE) == 0;
#else
    if (::linkat(oldfd, oldname, newfd, newname, 0) != 0) return false;
    if (::unlinkat(oldfd, oldname, 0) == 0) return true;
    (void)::unlinkat(newfd, newname, 0);
    return false;
#endif
}

}  // namespace

void inject_fs_fault(FsFault point, unsigned fail_after) noexcept
{
    faults().countdown.store(fail_after);
    faults().point.store(static_cast<int>(std::to_underlying(point)));
}
void clear_fs_faults() noexcept
{
    faults().point.store(-1);
    faults().countdown.store(0);
}

std::string object_relative_path(const ObjectId& id)
{
    std::string hex(32, '0');
    for (size_t i = 0; i < id.size(); ++i) {
        const std::byte byte{id[i]};
        hex[i * 2] = hex_digit(byte >> 4U);
        hex[i * 2 + 1] = hex_digit(byte);
    }
    return "objects/" + hex.substr(0, 2) + "/" + hex + ".osvo";
}

std::optional<ObjectId> parse_object_relative_path(std::string_view p) noexcept
{
    if (p.size() != 8 + 2 + 1 + 32 + 5 || !p.starts_with("objects/") || p[10] != '/' ||
        !p.ends_with(".osvo"))
        return std::nullopt;
    const auto shard = p.substr(8, 2);
    const auto hex = p.substr(11, 32);
    if (!hex.starts_with(shard)) return std::nullopt;
    ObjectId id{};
    for (size_t i = 0; i < id.size(); ++i) {
        const int hi = hex_value(hex[i * 2]);
        const int lo = hex_value(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        id[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return id;
}

DurableFile::DurableFile(int fd, int staging_fd, std::string name) noexcept
    : fd_(fd), staging_fd_(staging_fd), name_(std::move(name))
{}
DurableFile::~DurableFile()
{
    close();
}
DurableFile::DurableFile(DurableFile&& o) noexcept
    : fd_(std::exchange(o.fd_, -1)), staging_fd_(std::exchange(o.staging_fd_, -1)),
      name_(std::move(o.name_)), published_(o.published_), synced_(o.synced_)
{}
DurableFile& DurableFile::operator=(DurableFile&& o) noexcept
{
    if (this != &o) {
        close();
        fd_ = std::exchange(o.fd_, -1);
        staging_fd_ = std::exchange(o.staging_fd_, -1);
        name_ = std::move(o.name_);
        published_ = o.published_;
        synced_ = o.synced_;
    }
    return *this;
}
void DurableFile::close() noexcept
{
    close_fd(fd_);
    if (staging_fd_ >= 0 && !published_ && !name_.empty())
        (void)::unlinkat(staging_fd_, name_.c_str(), 0);
    close_fd(staging_fd_);
}
bool DurableFile::write_all(std::span<const uint8_t> bytes) noexcept
{
    synced_ = false;
    size_t off = 0;
    while (off < bytes.size()) {
        if (fail(FsFault::Write)) return false;
        // The descriptor was securely created beneath the retained staging directory; `bytes`
        // is payload, not a path. Sonar's path-taint model misclassifies write(2)'s buffer.
        const ssize_t n = ::write(  // NOSONAR cppsecurity:S2083
            fd_, bytes.data() + off, bytes.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}
bool DurableFile::sync() noexcept
{
    synced_ = !fail(FsFault::FileSync) && ::fdatasync(fd_) == 0;
    return synced_;
}

ObjectFile::~ObjectFile()
{
    close_fd(fd_);
}
ObjectFile::ObjectFile(ObjectFile&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
ObjectFile& ObjectFile::operator=(ObjectFile&& other) noexcept
{
    if (this != &other) {
        close_fd(fd_);
        fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
}
bool ObjectFile::read_at(uint64_t offset, std::span<uint8_t> out) const noexcept
{
    if (offset > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) return false;
    size_t done = 0;
    while (done < out.size()) {
        const uint64_t position = offset + done;
        if (position < offset ||
            position > static_cast<uint64_t>(std::numeric_limits<off_t>::max()))
            return false;
        const ssize_t n =
            ::pread(fd_, out.data() + done, out.size() - done, static_cast<off_t>(position));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}
std::optional<uint64_t> ObjectFile::size() const noexcept
{
    struct stat st{};
    if (::fstat(fd_, &st) != 0 || st.st_size < 0) return std::nullopt;
    return static_cast<uint64_t>(st.st_size);
}

WriterLock::~WriterLock()
{
    if (fd_ >= 0) {
        (void)::flock(fd_, LOCK_UN);
        ::close(fd_);
    }
}
WriterLock::WriterLock(WriterLock&& o) noexcept : fd_(std::exchange(o.fd_, -1)) {}
WriterLock& WriterLock::operator=(WriterLock&& o) noexcept
{
    if (this != &o) {
        if (fd_ >= 0) {
            (void)::flock(fd_, LOCK_UN);
            ::close(fd_);
        }
        fd_ = std::exchange(o.fd_, -1);
    }
    return *this;
}

VaultRoot::VaultRoot(int fd, std::filesystem::path display) noexcept
    : fd_(fd), display_path_(std::move(display))
{}
VaultRoot::~VaultRoot()
{
    close();
}
VaultRoot::VaultRoot(VaultRoot&& o) noexcept
    : fd_(std::exchange(o.fd_, -1)), display_path_(std::move(o.display_path_))
{}
VaultRoot& VaultRoot::operator=(VaultRoot&& o) noexcept
{
    if (this != &o) {
        close();
        fd_ = std::exchange(o.fd_, -1);
        display_path_ = std::move(o.display_path_);
    }
    return *this;
}
void VaultRoot::close() noexcept
{
    close_fd(fd_);
}

std::optional<VaultRoot> VaultRoot::create(const std::filesystem::path& path)
{
    const std::filesystem::path parent_path = path.has_parent_path() ? path.parent_path() : ".";
    const std::string leaf = path.filename().string();
    if (!plain_staging_name(leaf)) return std::nullopt;
    const int parent = open_root(parent_path);
    if (parent < 0 || ::mkdirat(parent, leaf.c_str(), 0700) != 0) {
        if (parent >= 0) ::close(parent);
        return std::nullopt;
    }
    int fd = -1;
    bool committed = false;
    const auto rollback = [&] {
        if (committed) return;
        if (fd >= 0) {
            for (const char* n : {"vault.header", "vault.db", "lock"})
                (void)::unlinkat(fd, n, 0);
            for (const char* n : {"objects", "staging"})
                (void)::unlinkat(fd, n, AT_REMOVEDIR);
            ::close(fd);
            fd = -1;
        }
        (void)::unlinkat(parent, leaf.c_str(), AT_REMOVEDIR);
    };
    if (::fchmodat(parent, leaf.c_str(), 0700, AT_SYMLINK_NOFOLLOW) != 0) {
        rollback();
        ::close(parent);
        return std::nullopt;
    }
    fd = open_child(parent, leaf.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        rollback();
        ::close(parent);
        return std::nullopt;
    }
    bool ok = true;
    for (const char* n : {"objects", "staging"})
        ok = ok && ::mkdirat(fd, n, 0700) == 0 && ::fchmodat(fd, n, 0700, 0) == 0;
    for (const char* n : {"vault.header", "vault.db", "lock"})
        ok = ok && create_file(fd, n);
    ok = ok && sync_dir(fd) && sync_dir(parent);
    if (!ok) {
        rollback();
        ::close(parent);
        return std::nullopt;
    }
    committed = true;
    ::close(parent);
    VaultRoot root(fd, path);
    if (!root.validate_layout()) return std::nullopt;
    return root;
}

std::optional<VaultRoot> VaultRoot::open(const std::filesystem::path& path)
{
    const int fd = open_root(path);
    if (fd < 0) return std::nullopt;
    VaultRoot root(fd, path);
    if (!root.validate_layout()) return std::nullopt;
    return root;
}

namespace {

std::optional<std::pair<unsigned, bool>> root_entry_kind(std::string_view name) noexcept
{
    if (name == "vault.header") return std::pair{1U, false};
    if (name == "vault.db") return std::pair{2U, false};
    if (name == "objects") return std::pair{4U, true};
    if (name == "staging") return std::pair{8U, true};
    if (name == "lock") return std::pair{16U, false};
    return std::nullopt;
}

bool validate_root_entry(int root_fd, const dirent& entry, unsigned& seen) noexcept
{
    const std::string_view name{entry.d_name};
    const auto kind = root_entry_kind(name);
    if (!kind || (seen & kind->first) != 0) return false;
    const bool directory = kind->second;
    const int child =
        open_child(root_fd, entry.d_name, directory ? O_RDONLY | O_DIRECTORY : O_RDONLY);
    const bool metadata_ok = child >= 0 && owned_mode(child, directory ? S_IFDIR : S_IFREG,
                                                      directory ? 0700 : 0600, !directory);
    const bool device_ok = metadata_ok && (!directory || same_device(root_fd, child));
    const bool contents_ok =
        device_ok && (!directory || validate_entries(child, name == "objects"));
    if (child >= 0) ::close(child);
    if (!contents_ok) return false;
    seen |= kind->first;
    return true;
}

}  // namespace

bool VaultRoot::validate_layout() const noexcept
{
    if (!owned_mode(fd_, S_IFDIR, 0700, false)) return false;
    // A dup shares the directory stream offset with the root descriptor, so a
    // second validation would otherwise begin at EOF. Re-open "." relative to
    // the held root to get an independent open file description.
    const int scanfd = open_child(fd_, ".", O_RDONLY | O_DIRECTORY);
    if (scanfd < 0) return false;
    DIR* dir = ::fdopendir(scanfd);
    if (!dir) {
        ::close(scanfd);
        return false;
    }
    unsigned seen = 0;
    errno = 0;
    while (const dirent* e = ::readdir(dir)) {
        if (const std::string_view name{e->d_name};
            name != "." && name != ".." && !validate_root_entry(fd_, *e, seen)) {
            ::closedir(dir);
            return false;
        }
    }
    const bool ok = errno == 0 && seen == 31;
    ::closedir(dir);
    return ok;
}

std::optional<WriterLock> VaultRoot::try_writer_lock() const noexcept
{
    if (fail(FsFault::Lock)) return std::nullopt;
    const int fd = open_child(fd_, "lock", O_RDWR);
    if (fd < 0 || !owned_mode(fd, S_IFREG, 0600, true) || ::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        if (fd >= 0) ::close(fd);
        return std::nullopt;
    }
    const std::string pid = std::format("{}\n", ::getpid());
    const int truncate_result = ::ftruncate(fd, 0);
    if (const ssize_t write_result =
            truncate_result == 0 ? ::pwrite(fd, pid.data(), pid.size(), 0) : -1;
        write_result == static_cast<ssize_t>(pid.size()))
        (void)::fdatasync(fd);
    return WriterLock(fd);
}

std::optional<DurableFile> VaultRoot::create_staging_file() const noexcept
{
    const int staging = open_child(fd_, "staging", O_RDONLY | O_DIRECTORY);
    if (staging < 0 || !owned_mode(staging, S_IFDIR, 0700, false)) {
        if (staging >= 0) ::close(staging);
        return std::nullopt;
    }
    for (int attempt = 0; attempt < 64; ++attempt) {
        std::array<uint8_t, 16> random{};
        if (!crypto::fill_random(random)) {
            ::close(staging);
            return std::nullopt;
        }
        std::string name = "tmp-";
        for (std::byte byte : std::as_bytes(std::span{random})) {
            name.push_back(hex_digit(byte >> 4U));
            name.push_back(hex_digit(byte));
        }
        if (const int fd = open_child(staging, name.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
            fd >= 0) {
            (void)::fchmod(fd, 0600);
            return DurableFile(fd, staging, std::move(name));
        }
        if (errno != EEXIST) {
            ::close(staging);
            return std::nullopt;
        }
    }
    ::close(staging);
    return std::nullopt;
}

std::optional<ObjectFile> VaultRoot::open_object(const ObjectId& id) const noexcept
{
    if (std::ranges::all_of(id, [](uint8_t byte) { return byte == 0; })) return std::nullopt;
    const std::string rel = object_relative_path(id);
    const std::string shard = rel.substr(8, 2);
    const std::string name = rel.substr(11);
    const int objects = open_child(fd_, "objects", O_RDONLY | O_DIRECTORY);
    if (objects < 0) return std::nullopt;
    const int shard_fd = open_child(objects, shard.c_str(), O_RDONLY | O_DIRECTORY);
    ::close(objects);
    if (shard_fd < 0) return std::nullopt;
    const int object_fd = open_child(shard_fd, name.c_str(), O_RDONLY);
    ::close(shard_fd);
    if (object_fd < 0 || !owned_mode(object_fd, S_IFREG, 0600, true)) {
        if (object_fd >= 0) ::close(object_fd);
        return std::nullopt;
    }
    return ObjectFile(object_fd);
}

bool VaultRoot::publish(DurableFile& file, const ObjectId& id) const noexcept
{
    if (!file.valid() || file.published_ || !file.synced_) return false;
    const std::string rel = object_relative_path(id);
    const std::string shard = rel.substr(8, 2);
    const std::string name = rel.substr(11);
    const int objects = open_child(fd_, "objects", O_RDONLY | O_DIRECTORY);
    if (objects < 0) return false;
    int shard_fd = -1;
    bool ok = ensure_shard(objects, shard, shard_fd);
    if (ok) ok = rename_noreplace(file.staging_fd_, file.name_.c_str(), shard_fd, name.c_str());
    if (ok) {
        file.published_ = true;
        ok = sync_dir(shard_fd) && sync_dir(file.staging_fd_);
    }
    if (shard_fd >= 0) ::close(shard_fd);
    ::close(objects);
    return ok;
}

bool VaultRoot::unlink_object(const ObjectId& id) const noexcept
{
    const std::string rel = object_relative_path(id);
    const std::string shard = rel.substr(8, 2);
    const std::string name = rel.substr(11);
    const int objects = open_child(fd_, "objects", O_RDONLY | O_DIRECTORY);
    if (objects < 0) return false;
    const int shardfd = open_child(objects, shard.c_str(), O_RDONLY | O_DIRECTORY);
    ::close(objects);
    if (shardfd < 0) return false;
    const bool ok =
        !fail(FsFault::Unlink) && ::unlinkat(shardfd, name.c_str(), 0) == 0 && sync_dir(shardfd);
    ::close(shardfd);
    return ok;
}

std::optional<std::vector<ObjectEntry>> VaultRoot::list_objects() const noexcept
{
    std::vector<ObjectEntry> result;
    const int objects = open_child(fd_, "objects", O_RDONLY | O_DIRECTORY);
    if (objects < 0) return std::nullopt;
    const bool ok = scan_objects(objects, result);
    ::close(objects);
    if (!ok) return std::nullopt;
    return result;
}

bool VaultRoot::read_header(std::span<uint8_t> destination) const noexcept
{
    const int header = open_child(fd_, "vault.header", O_RDONLY);
    if (header < 0 || !owned_mode(header, S_IFREG, 0600, true)) {
        if (header >= 0) ::close(header);
        return false;
    }
    size_t done = 0;
    while (done < destination.size()) {
        const ssize_t got = ::pread(header, destination.data() + done, destination.size() - done,
                                    static_cast<off_t>(done));
        if (got <= 0) {
            ::close(header);
            return false;
        }
        done += static_cast<size_t>(got);
    }
    struct stat st{};
    const bool exact =
        ::fstat(header, &st) == 0 && static_cast<uint64_t>(st.st_size) == destination.size();
    ::close(header);
    return exact;
}

bool VaultRoot::write_header(std::span<const uint8_t> bytes) const noexcept
{
    if (bytes.empty() || fail(FsFault::Write)) return false;
    const int header = open_child(fd_, "vault.header", O_WRONLY);
    if (header < 0 || !owned_mode(header, S_IFREG, 0600, true)) {
        if (header >= 0) ::close(header);
        return false;
    }
    bool ok = ::ftruncate(header, 0) == 0;
    size_t done = 0;
    while (ok && done < bytes.size()) {
        // `header` was descriptor-opened with O_NOFOLLOW; `bytes` is payload, not a path.
        const ssize_t wrote = ::pwrite( // NOSONAR cppsecurity:S2083
            header, bytes.data() + done, bytes.size() - done, static_cast<off_t>(done));
        if (wrote <= 0)
            ok = false;
        else
            done += static_cast<size_t>(wrote);
    }
    ok = ok && !fail(FsFault::FileSync) && ::fdatasync(header) == 0;
    ::close(header);
    return ok && !fail(FsFault::DirectorySync) && sync_dir(fd_);
}

bool VaultRoot::replace_header(std::span<const uint8_t> bytes) const noexcept
{
    if (bytes.empty()) return false;
    auto staged = create_staging_file();
    if (!staged || !staged->write_all(bytes) || !staged->sync()) return false;
    if (fail(FsFault::Rename) ||
        ::renameat(staged->staging_fd_, staged->name_.c_str(), fd_, "vault.header") != 0)
        return false;
    staged->published_ = true;
    return !fail(FsFault::DirectorySync) && sync_dir(fd_) && sync_dir(staged->staging_fd_);
}

bool VaultRoot::rollback_creation() noexcept
{
    if (fd_ < 0 || display_path_.filename().empty()) return false;
    const std::filesystem::path parent_path =
        display_path_.has_parent_path() ? display_path_.parent_path() : ".";
    const std::string leaf = display_path_.filename().string();
    const int parent = open_root(parent_path);
    struct stat held{};
    struct stat named{}; // NOSONAR cpp:S6004 -- compared with the retained descriptor below
    if (parent < 0 || ::fstat(fd_, &held) != 0 ||
        ::fstatat(parent, leaf.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0 ||
        held.st_dev != named.st_dev || held.st_ino != named.st_ino || !S_ISDIR(named.st_mode)) {
        if (parent >= 0) ::close(parent);
        return false;
    }
    bool ok = true;
    for (const char* name : {"vault.header", "vault.db", "lock"})
        if (::unlinkat(fd_, name, 0) != 0 && errno != ENOENT) ok = false;
    for (const char* name : {"objects", "staging"})
        if (::unlinkat(fd_, name, AT_REMOVEDIR) != 0 && errno != ENOENT) ok = false;
    // `leaf` is one filename component and the inode was matched to our retained root fd above.
    if (ok) // NOSONAR cppsecurity:S2083
        ok = ::unlinkat(parent, leaf.c_str(), AT_REMOVEDIR) == 0 && sync_dir(parent);
    ::close(parent);
    if (ok) close();
    return ok;
}

bool VaultRoot::unlink_staging(std::string_view name) const noexcept
{
    if (!plain_staging_name(name) || fail(FsFault::Unlink)) return false;
    const int fd = open_child(fd_, "staging", O_RDONLY | O_DIRECTORY);
    if (fd < 0) return false;
    const std::string n{name};
    const bool ok = ::unlinkat(fd, n.c_str(), 0) == 0 && sync_dir(fd);
    ::close(fd);
    return ok;
}

std::optional<StagingCleanup> VaultRoot::cleanup_staging(const WriterLock&,
                                                         int64_t older_than_seconds) const noexcept
{
    StagingCleanup result;
    const int staging = open_child(fd_, "staging", O_RDONLY | O_DIRECTORY);
    if (staging < 0) return std::nullopt;
    const int scan_fd = open_child(staging, ".", O_RDONLY | O_DIRECTORY);
    DIR* entries = scan_fd >= 0 ? ::fdopendir(scan_fd) : nullptr;
    if (!entries) {
        if (scan_fd >= 0) ::close(scan_fd);
        ::close(staging);
        return std::nullopt;
    }
    const bool ok = remove_old_staging_entries(staging, entries, older_than_seconds, result);
    ::closedir(entries);
    ::close(staging);
    return ok ? std::optional{result} : std::nullopt;
}

}  // namespace vault::v3
