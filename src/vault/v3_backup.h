#pragma once

#include "vault/v3_db.h"
#include "vault/v3_header.h"
#include "vault/v3_recovery.h"

#include <filesystem>

namespace vault::v3 {

enum class BackupStatus : uint8_t {
    Ok,
    InvalidArgument,
    AlreadyExists,
    SourceCorrupt,
    InsufficientSpace,
    IoError,
};

struct BackupResult {
    BackupStatus status = BackupStatus::IoError;
    uint64_t copied_bytes = 0;
    size_t copied_objects = 0;
    uint64_t required_bytes = 0;
    uint64_t available_bytes = 0;
};

[[nodiscard]] std::optional<uint64_t> backup_space_required(uint64_t database_bytes,
                                                            uint64_t object_bytes) noexcept;

// Creates a complete encrypted snapshot beside `destination`, deep-verifies it, then publishes
// the directory no-replace. `writer_lock` is the consistency barrier for the DB snapshot and its
// immutable object set. Source files are never modified.
[[nodiscard]] BackupResult backup_vault(const VaultRoot& source, const WriterLock& writer_lock,
                                        const Database& database,
                                        std::span<const uint8_t, crypto::KEY_SIZE> database_key,
                                        std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                                        const V3Header& header,
                                        const std::filesystem::path& destination) noexcept;

// Validates and copies a closed v3 backup into a new path. It never overlays `destination` and
// never modifies the source, including on authentication or integrity failure.
[[nodiscard]] BackupResult restore_vault(const std::filesystem::path& source,
                                         std::span<const uint8_t> password,
                                         std::span<const uint8_t> keyfile,
                                         const std::filesystem::path& destination) noexcept;

}  // namespace vault::v3
