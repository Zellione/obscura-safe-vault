#pragma once

#include "crypto/secure_mem.h"
#include "vault/index.h"
#include "vault/v3_backup.h"
#include "vault/v3_db.h"
#include "vault/v3_fs.h"
#include "vault/v3_header.h"
#include "vault/v3_object_store.h"
#include "vault/v3_recovery.h"

#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace media {
class VideoSource;
}

namespace vault::v3 {

inline uint64_t object_cache_identity(const Id& id, uint8_t role) noexcept
{
    uint64_t value = 1469598103934665603ULL ^ role;
    for (const uint8_t byte : id)
        value = (value ^ byte) * 1099511628211ULL;
    return value == 0 ? role : value;
}

enum class ReadStatus : uint8_t {
    Ok,
    IoError,
    BadFormat,
    AuthenticationFailed,
    Busy,
    UnsupportedVersion,
    AlreadyExists
};

class ReadSession {
public:
    struct OpenResult {
        ReadStatus status = ReadStatus::IoError;
        std::unique_ptr<ReadSession> session;
    };

    [[nodiscard]] static OpenResult open(const std::filesystem::path& path);
    [[nodiscard]] static OpenResult create(const std::filesystem::path& path,
                                           std::span<const uint8_t> password,
                                           std::span<const uint8_t> keyfile,
                                           const crypto::KdfParams& kdf);
    [[nodiscard]] ReadStatus unlock(std::span<const uint8_t> password,
                                    std::span<const uint8_t> keyfile);
    void lock() noexcept;
    [[nodiscard]] bool unlocked() const noexcept
    {
        return unlocked_;
    }
    [[nodiscard]] const VerificationReport& quick_open_report() const noexcept
    {
        return quick_open_report_;
    }
    [[nodiscard]] const IndexNode& root() const noexcept
    {
        return root_;
    }
    [[nodiscard]] const VaultSettings& settings() const noexcept
    {
        return settings_;
    }
    [[nodiscard]] const std::vector<SavedSearch>& saved_searches() const noexcept
    {
        return saved_searches_;
    }
    [[nodiscard]] ReadStatus read(const IndexNode& node, ObjectRole role,
                                  crypto::SecureBytes& out) const noexcept;
    [[nodiscard]] ReadStatus read_id(const Id& node_id, ObjectRole role,
                                     crypto::SecureBytes& out) const noexcept;
    [[nodiscard]] ReadStatus read_video_frame(const Id& node_id, uint32_t sequence,
                                              crypto::SecureBytes& out) const noexcept;
    [[nodiscard]] std::optional<uint64_t>
    object_plaintext_length(const Id& node_id, ObjectRole role) const noexcept;
    [[nodiscard]] ReadStatus digest_object(const Id& node_id, ObjectRole role,
                                           std::span<const uint8_t> digest_key,
                                           std::span<uint8_t> digest,
                                           CancellationToken cancel = {}) const noexcept;
    [[nodiscard]] ReadStatus commit_metadata(const IndexNode& root, const VaultSettings& settings,
                                             std::span<const SavedSearch> searches) noexcept;
    [[nodiscard]] ReadStatus stage_object(const Id& node_id, ObjectRole role, uint8_t media_format,
                                          std::span<const uint8_t> plaintext,
                                          ObjectInfo* info = nullptr) noexcept;
    [[nodiscard]] ReadStatus stage_object_stream(const Id& node_id, ObjectRole role,
                                                 uint8_t media_format, uint64_t plaintext_length,
                                                 const ObjectReadFn& read,
                                                 ObjectInfo* info = nullptr) noexcept;
    [[nodiscard]] ReadStatus change_password(std::span<const uint8_t> old_password,
                                             std::span<const uint8_t> old_keyfile,
                                             std::span<const uint8_t> new_password,
                                             std::span<const uint8_t> new_keyfile) noexcept;
    [[nodiscard]] VerificationReport verify(VerifyDepth depth,
                                            CancellationToken cancel = {}) const noexcept;
    [[nodiscard]] GarbageCollectionResult garbage_collect(uint64_t grace_seconds) noexcept;
    [[nodiscard]] BackupResult backup(const std::filesystem::path& destination) const noexcept;
    [[nodiscard]] DbStatus maintain_database_storage() noexcept;
    [[nodiscard]] std::optional<uint64_t> database_bytes() const noexcept;
    [[nodiscard]] DbStatus begin_conversion(const ConversionMarker& marker) noexcept;
    [[nodiscard]] DbResult<std::optional<ConversionMarker>> conversion_marker() const noexcept;
    [[nodiscard]] DbResult<std::optional<Id>>
    conversion_node_id(const Id& source_node_id) const noexcept;
    [[nodiscard]] DbStatus record_conversion_node(const ConversionNode& node) noexcept;
    [[nodiscard]] DbStatus finish_conversion() noexcept;

private:
    friend class ::media::VideoSource;
    [[nodiscard]] bool materialize() noexcept;
    void rebuild_object_index();
    [[nodiscard]] bool build_children(IndexNode& parent, const Id& parent_id, unsigned depth,
                                      size_t& count) noexcept;
    [[nodiscard]] std::optional<ObjectInfo> object_for(const Id& node_id,
                                                       ObjectRole role) const noexcept;

    VaultRoot root_handle_;
    V3Header header_;
    std::optional<WriterLock> session_lock_;
    crypto::SecureBuffer<crypto::KEY_SIZE> master_key_;
    std::optional<Database> database_;
    std::vector<ObjectRecord> objects_;
    std::map<std::pair<Id, ObjectRole>, ObjectInfo> object_index_;
    std::vector<ObjectRecord> staged_objects_;
    std::map<std::pair<Id, ObjectRole>, ObjectInfo> staged_index_;
    IndexNode root_ = IndexNode::gallery("");
    VaultSettings settings_;
    std::vector<SavedSearch> saved_searches_;
    bool unlocked_ = false;
    VerificationReport quick_open_report_;
    mutable std::mutex state_mutex_;
};

}  // namespace vault::v3
