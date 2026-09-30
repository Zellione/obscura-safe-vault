#pragma once

#include "crypto/secure_mem.h"
#include "vault/index.h"
#include "vault/v3_db.h"
#include "vault/v3_fs.h"
#include "vault/v3_header.h"
#include "vault/v3_object_store.h"

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace media { class VideoSource; }

namespace vault::v3 {

enum class ReadStatus : uint8_t { Ok, IoError, BadFormat, AuthenticationFailed, Busy,
                                  UnsupportedVersion };

class ReadSession {
public:
    struct OpenResult {
        ReadStatus status = ReadStatus::IoError;
        std::unique_ptr<ReadSession> session;
    };

    [[nodiscard]] static OpenResult open(const std::filesystem::path& path);
    [[nodiscard]] ReadStatus unlock(std::span<const uint8_t> password,
                                    std::span<const uint8_t> keyfile);
    void lock() noexcept;
    [[nodiscard]] bool unlocked() const noexcept { return unlocked_; }
    [[nodiscard]] const IndexNode& root() const noexcept { return root_; }
    [[nodiscard]] const VaultSettings& settings() const noexcept { return settings_; }
    [[nodiscard]] const std::vector<SavedSearch>& saved_searches() const noexcept
    { return saved_searches_; }
    [[nodiscard]] ReadStatus read(const IndexNode& node, ObjectRole role,
                                  crypto::SecureBytes& out) const noexcept;
    [[nodiscard]] ReadStatus read_id(const Id& node_id, ObjectRole role,
                                     crypto::SecureBytes& out) const noexcept;

private:
    friend class ::media::VideoSource;
    [[nodiscard]] bool materialize() noexcept;
    [[nodiscard]] bool build_children(IndexNode& parent, const Id& parent_id,
                                      unsigned depth, size_t& count) noexcept;
    [[nodiscard]] std::optional<ObjectInfo> object_for(const Id& node_id,
                                                       ObjectRole role) const noexcept;

    VaultRoot root_handle_;
    V3Header header_;
    std::optional<WriterLock> session_lock_;
    crypto::SecureBuffer<crypto::KEY_SIZE> master_key_;
    std::optional<Database> database_;
    std::vector<ObjectRecord> objects_;
    IndexNode root_ = IndexNode::gallery("");
    VaultSettings settings_;
    std::vector<SavedSearch> saved_searches_;
    bool unlocked_ = false;
};

} // namespace vault::v3
