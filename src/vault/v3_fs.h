#pragma once

#include "vault/v3_crypto_spec.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vault::v3 {

using ObjectId = Id;

struct ObjectEntry {
    ObjectId id{};
    uint64_t size = 0;
    int64_t modified_seconds = 0;
};

struct StagingCleanup {
    size_t removed = 0;
    size_t preserved_recent = 0;
    size_t quarantined_or_foreign = 0;
};

enum class FsFault : uint8_t { Open, Write, FileSync, Rename, DirectorySync, Lock, Unlink };
void inject_fs_fault(FsFault point, unsigned fail_after = 0) noexcept;
void clear_fs_faults() noexcept;

[[nodiscard]] std::string object_relative_path(const ObjectId& id);
[[nodiscard]] std::optional<ObjectId> parse_object_relative_path(std::string_view path) noexcept;

class DurableFile {
public:
    DurableFile() = default;
    ~DurableFile();
    DurableFile(DurableFile&& other) noexcept;
    DurableFile& operator=(DurableFile&& other) noexcept;
    DurableFile(const DurableFile&) = delete;
    DurableFile& operator=(const DurableFile&) = delete;

    [[nodiscard]] bool write_all(std::span<const uint8_t> bytes) noexcept;
    [[nodiscard]] bool sync() noexcept;
    [[nodiscard]] bool valid() const noexcept
    {
        return fd_ >= 0;
    }

private:
    friend class VaultRoot;
    DurableFile(int fd, int staging_fd, std::string name) noexcept;
    void close() noexcept;
    int fd_ = -1;
    int staging_fd_ = -1;
    std::string name_;
    bool published_ = false;
    bool synced_ = false;
};

class ObjectFile {
public:
    ObjectFile() = default;
    ~ObjectFile();
    ObjectFile(ObjectFile&& other) noexcept;
    ObjectFile& operator=(ObjectFile&& other) noexcept;
    ObjectFile(const ObjectFile&) = delete;
    ObjectFile& operator=(const ObjectFile&) = delete;

    [[nodiscard]] bool read_at(uint64_t offset, std::span<uint8_t> out) const noexcept;
    [[nodiscard]] std::optional<uint64_t> size() const noexcept;

private:
    friend class VaultRoot;
    explicit ObjectFile(int fd) noexcept : fd_(fd) {}
    int fd_ = -1;
};

class WriterLock {
public:
    WriterLock() = default;
    ~WriterLock();
    WriterLock(WriterLock&& other) noexcept;
    WriterLock& operator=(WriterLock&& other) noexcept;
    WriterLock(const WriterLock&) = delete;
    WriterLock& operator=(const WriterLock&) = delete;

private:
    friend class VaultRoot;
    explicit WriterLock(int fd) noexcept : fd_(fd) {}
    int fd_ = -1;
};

class VaultRoot {
public:
    VaultRoot() = default;
    ~VaultRoot();
    VaultRoot(VaultRoot&& other) noexcept;
    VaultRoot& operator=(VaultRoot&& other) noexcept;
    VaultRoot(const VaultRoot&) = delete;
    VaultRoot& operator=(const VaultRoot&) = delete;

    [[nodiscard]] static std::optional<VaultRoot> create(const std::filesystem::path& path);
    [[nodiscard]] static std::optional<VaultRoot> open(const std::filesystem::path& path);
    [[nodiscard]] bool validate_layout() const noexcept;
    [[nodiscard]] std::optional<WriterLock> try_writer_lock() const noexcept;
    [[nodiscard]] std::optional<DurableFile> create_staging_file() const noexcept;
    [[nodiscard]] std::optional<ObjectFile> open_object(const ObjectId& id) const noexcept;
    [[nodiscard]] bool publish(DurableFile& file, const ObjectId& id) const noexcept;
    [[nodiscard]] bool unlink_object(const ObjectId& id) const noexcept;
    [[nodiscard]] std::optional<std::vector<ObjectEntry>> list_objects() const noexcept;
    // Control files have fixed application-owned names; callers never pass user metadata.
    [[nodiscard]] bool read_header(std::span<uint8_t> destination) const noexcept;
    [[nodiscard]] bool write_header(std::span<const uint8_t> bytes) const noexcept;
    [[nodiscard]] bool replace_header(std::span<const uint8_t> bytes) const noexcept;
    [[nodiscard]] bool sync_database() const noexcept;
    [[nodiscard]] std::optional<uint64_t> database_size() const noexcept;
    // Creation-only rollback. Refuses to remove a path whose inode no longer
    // matches the descriptor retained by this instance.
    [[nodiscard]] bool rollback_creation() noexcept;
    // Atomically publishes this newly-created directory under a sibling name. The destination
    // must not exist; the retained descriptor prevents source-path substitution.
    [[nodiscard]] bool publish_directory(const std::filesystem::path& destination) noexcept;
    [[nodiscard]] bool unlink_staging(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<StagingCleanup>
    cleanup_staging(const WriterLock& writer_lock, int64_t older_than_seconds) const noexcept;
    [[nodiscard]] int native_handle() const noexcept
    {
        return fd_;
    }

private:
    explicit VaultRoot(int fd, std::filesystem::path display) noexcept;
    void close() noexcept;
    int fd_ = -1;
    std::filesystem::path display_path_;
};

}  // namespace vault::v3
