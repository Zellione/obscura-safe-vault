#include "vault/v3_mutation.h"

#include <algorithm>

namespace vault::v3 {

MutationCoordinator::MutationCoordinator(
    VaultRoot& root, Database& database, WriterLock lock,
    std::span<const uint8_t, crypto::KEY_SIZE> key) noexcept
    : root_(&root), database_(&database), lock_(std::move(lock))
{
    std::ranges::copy(key, master_key_.span().begin());
}

std::optional<MutationCoordinator>
MutationCoordinator::open(VaultRoot& root, Database& database,
                          std::span<const uint8_t, crypto::KEY_SIZE> master_key) noexcept
{
    auto lock = root.try_writer_lock();
    if (!lock || !database.healthy()) return std::nullopt;
    return MutationCoordinator{root, database, std::move(*lock), master_key};
}

ObjectRecord MutationCoordinator::record_for(const ObjectInfo& info) const noexcept
{
    return {info.object_id, info.owner_node_id, info.role, info.encrypted_length,
            info.plaintext_length, info.frame_plain_limit, info.frame_count, 0};
}

MutationResult MutationCoordinator::create(const ObjectWriteRequest& request,
                                           std::span<const uint8_t> plaintext) noexcept
{
    const auto written = write_object(*root_, master_key_.as_span(), request, plaintext);
    if (written.status != ObjectStatus::Ok)
        return {MutationStatus::ObjectWriteFailed, written.info.object_id};
    if (database_->commit_object_create(record_for(written.info)) != DbStatus::Ok)
        return {MutationStatus::DatabaseFailed, written.info.object_id};
    return {MutationStatus::Ok, written.info.object_id};
}

MutationResult MutationCoordinator::replace(const ObjectWriteRequest& request,
                                            std::span<const uint8_t> plaintext) noexcept
{
    const auto written = write_object(*root_, master_key_.as_span(), request, plaintext);
    if (written.status != ObjectStatus::Ok)
        return {MutationStatus::ObjectWriteFailed, written.info.object_id};
    const auto replaced = database_->commit_object_replace(record_for(written.info));
    if (replaced.status != DbStatus::Ok)
        return {MutationStatus::DatabaseFailed, written.info.object_id};
    if (replaced.value && !root_->unlink_object(*replaced.value))
        return {MutationStatus::CleanupPending, written.info.object_id};
    return {MutationStatus::Ok, written.info.object_id};
}

MutationStatus MutationCoordinator::remove(const Id& node_id, ObjectRole role) noexcept
{
    const auto removed = database_->commit_object_delete(node_id, role);
    if (removed.status != DbStatus::Ok) return MutationStatus::DatabaseFailed;
    if (removed.value && !root_->unlink_object(*removed.value))
        return MutationStatus::CleanupPending;
    return MutationStatus::Ok;
}

}  // namespace vault::v3
