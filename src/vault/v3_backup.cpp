#include "vault/v3_backup.h"

#include "crypto/random.h"

#include <array>
#include <format>

namespace vault::v3 {
std::optional<uint64_t> backup_space_required(uint64_t database_bytes,
                                              uint64_t object_bytes) noexcept
{
    // SQLCipher online backup may transiently hold its destination plus a rollback journal.
    // Reserve two database images, the immutable objects, control files, and 1 MiB slack.
    constexpr uint64_t SLACK = 1024U * 1024U;
    if (object_bytes > UINT64_MAX - SLACK) return std::nullopt;
    if (database_bytes > (UINT64_MAX - object_bytes - SLACK) / 2U) return std::nullopt;
    return database_bytes * 2U + object_bytes + SLACK;
}
namespace {

std::optional<std::filesystem::path> temporary_path_for(const std::filesystem::path& destination)
{
    if (destination.filename().empty()) return std::nullopt;
    std::array<uint8_t, 8> random{};
    if (!crypto::fill_random(random)) return std::nullopt;
    std::string suffix;
    for (const auto byte : random)
        suffix += std::format("{:02x}", byte);
    const auto parent =
        destination.has_parent_path() ? destination.parent_path() : std::filesystem::path{"."};
    return parent / (".osv-backup-" + suffix);
}

bool copy_object(const VaultRoot& source, const VaultRoot& destination, const ObjectRecord& object)
{
    auto input = source.open_object(object.object_id);
    auto output = destination.create_staging_file();
    if (!input || !output) return false;
    std::array<uint8_t, 1024 * 1024> bytes{};
    uint64_t offset = 0;
    while (offset < object.encrypted_length) {
        const auto count =
            static_cast<size_t>(std::min<uint64_t>(bytes.size(), object.encrypted_length - offset));
        if (const std::span chunk{bytes.data(), count};
            !input->read_at(offset, chunk) || !output->write_all(chunk))
            return false;
        offset += count;
    }
    return output->sync() && destination.publish(*output, object.object_id);
}

bool populate_backup(const VaultRoot& source, VaultRoot& backup, const Database& database,
                     std::span<const uint8_t, crypto::KEY_SIZE> database_key,
                     std::span<const uint8_t, crypto::KEY_SIZE> master_key, const V3Header& header,
                     BackupResult& result) noexcept
{
    if (const auto raw_header = serialize_v3_header(header); !backup.write_header(raw_header))
        return false;
    const auto references = database.object_references();
    if (references.status != DbStatus::Ok) return false;
    for (const auto& object : references.value) {
        if (!copy_object(source, backup, object)) return false;
        ++result.copied_objects;
        result.copied_bytes += object.encrypted_length;
    }
    if (database.backup_to(backup, database_key) != DbStatus::Ok) return false;
    auto copied_db = Database::open_read_only(backup, database_key);
    if (!copied_db.database) return false;
    const bool valid =
        verify_vault(backup, *copied_db.database, master_key, header.vault_id, VerifyDepth::Deep)
            .status == RecoveryStatus::Ok;
    copied_db.database.reset();
    return valid;
}

}  // namespace

BackupResult backup_vault(const VaultRoot& source, const WriterLock&, const Database& database,
                          std::span<const uint8_t, crypto::KEY_SIZE> database_key,
                          std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                          const V3Header& header, const std::filesystem::path& destination) noexcept
{
    BackupResult result;
    std::error_code error;
    if (destination.filename().empty()) {
        result.status = BackupStatus::InvalidArgument;
        return result;
    }
    if (std::filesystem::exists(destination, error) || error) {
        result.status = error ? BackupStatus::IoError : BackupStatus::AlreadyExists;
        return result;
    }
    const auto source_health =
        verify_vault(source, database, master_key, header.vault_id, VerifyDepth::Deep);
    if (source_health.status != RecoveryStatus::Ok) {
        result.status = BackupStatus::SourceCorrupt;
        return result;
    }
    const auto db_bytes = source.database_size();
    const auto required =
        db_bytes ? backup_space_required(*db_bytes, source_health.referenced_bytes) : std::nullopt;
    const auto parent =
        destination.has_parent_path() ? destination.parent_path() : std::filesystem::path{"."};
    const auto space = std::filesystem::space(parent, error);
    if (!required.has_value() || error) return result;
    result.required_bytes = *required;
    result.available_bytes = space.available;
    if (space.available < *required) {
        result.status = BackupStatus::InsufficientSpace;
        return result;
    }
    const auto temporary = temporary_path_for(destination);
    if (!temporary) return result;
    auto backup = VaultRoot::create(*temporary);
    if (!backup) return result;
    const auto rollback = [&] {
        if (const auto objects = backup->list_objects())
            for (const auto& object : *objects)
                (void)backup->unlink_object(object.id);
        (void)backup->rollback_creation();
    };
    if (!populate_backup(source, *backup, database, database_key, master_key, header, result)) {
        rollback();
        return result;
    }
    if (!backup->publish_directory(destination)) {
        rollback();
        result.status = std::filesystem::exists(destination, error) && !error
                            ? BackupStatus::AlreadyExists
                            : BackupStatus::IoError;
        return result;
    }
    result.status = BackupStatus::Ok;
    return result;
}

BackupResult restore_vault(const std::filesystem::path& source_path,
                           std::span<const uint8_t> password, std::span<const uint8_t> keyfile,
                           const std::filesystem::path& destination) noexcept
{
    auto source = VaultRoot::open(source_path);
    if (!source) return {.status = BackupStatus::SourceCorrupt};
    auto writer = source->try_writer_lock();
    if (!writer) return {.status = BackupStatus::IoError};
    std::array<uint8_t, V3_HEADER_SIZE> raw{};
    V3Header header;
    if (!source->read_header(raw) || parse_v3_header(raw, header) != HeaderStatus::Ok)
        return {.status = BackupStatus::SourceCorrupt};
    crypto::SecureBuffer<crypto::KEY_SIZE> master;
    if (unwrap_v3_master_key(header, password, keyfile, master) != HeaderStatus::Ok)
        return {.status = BackupStatus::SourceCorrupt};
    auto database_key = derive_database_key(master.as_span(), header.vault_id);
    auto database = Database::open_read_only(*source, database_key.as_span());
    if (!database.database) return {.status = BackupStatus::SourceCorrupt};
    return backup_vault(*source, *writer, *database.database, database_key.as_span(),
                        master.as_span(), header, destination);
}

}  // namespace vault::v3
