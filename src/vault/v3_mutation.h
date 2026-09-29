#pragma once

#include "crypto/secure_mem.h"
#include "vault/v3_db.h"
#include "vault/v3_object_store.h"

#include <optional>
#include <span>

namespace vault::v3 {

enum class MutationStatus : uint8_t {
    Ok,
    InvalidArgument,
    Busy,
    ObjectWriteFailed,
    DatabaseFailed,
    CleanupPending,
};

struct MutationResult {
    MutationStatus status = MutationStatus::InvalidArgument;
    ObjectId object_id{};
    [[nodiscard]] bool committed() const noexcept
    {
        return status == MutationStatus::Ok || status == MutationStatus::CleanupPending;
    }
};

class MutationCoordinator {
public:
    [[nodiscard]] static std::optional<MutationCoordinator>
    open(VaultRoot& root, Database& database,
         std::span<const uint8_t, crypto::KEY_SIZE> master_key) noexcept;

    MutationCoordinator(MutationCoordinator&&) noexcept = default;
    MutationCoordinator& operator=(MutationCoordinator&&) noexcept = default;
    MutationCoordinator(const MutationCoordinator&) = delete;
    MutationCoordinator& operator=(const MutationCoordinator&) = delete;

    [[nodiscard]] MutationResult create(const ObjectWriteRequest& request,
                                        std::span<const uint8_t> plaintext) noexcept;
    [[nodiscard]] MutationResult replace(const ObjectWriteRequest& request,
                                         std::span<const uint8_t> plaintext) noexcept;
    [[nodiscard]] MutationStatus remove(const Id& node_id, ObjectRole role) noexcept;
    [[nodiscard]] const WriterLock& writer_lock() const noexcept { return lock_; }

private:
    MutationCoordinator(VaultRoot& root, Database& database, WriterLock lock,
                        std::span<const uint8_t, crypto::KEY_SIZE> key) noexcept;
    [[nodiscard]] ObjectRecord record_for(const ObjectInfo& info) const noexcept;

    VaultRoot* root_ = nullptr;
    Database* database_ = nullptr;
    WriterLock lock_;
    crypto::SecureBuffer<crypto::KEY_SIZE> master_key_;
};

}  // namespace vault::v3
