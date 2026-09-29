#pragma once

#include "vault/v3_db.h"
#include "vault/v3_fs.h"

#include <cstdint>
#include <span>
#include <vector>

namespace vault::v3 {

enum class RecoveryStatus : uint8_t { Ok, DatabaseError, FilesystemError, Corrupt };

struct GarbageObject {
    ObjectId id{};
    uint64_t encrypted_length = 0;
    int64_t modified_seconds = 0;
};

struct ReconciliationReport {
    RecoveryStatus status = RecoveryStatus::FilesystemError;
    uint64_t referenced_bytes = 0;
    uint64_t garbage_bytes = 0;
    std::vector<ObjectId> missing;
    std::vector<ObjectId> size_mismatches;
    std::vector<GarbageObject> garbage;
};

struct GarbageCollectionResult {
    RecoveryStatus status = RecoveryStatus::FilesystemError;
    size_t deleted = 0;
    uint64_t reclaimed_bytes = 0;
};

// The DB query is one SQLite read statement, hence one consistent snapshot. Filenames are
// enumerated descriptor-relatively and accepted only through the canonical object grammar.
[[nodiscard]] ReconciliationReport reconcile_objects(const VaultRoot& root,
                                                      const Database& database) noexcept;

// `writer_lock` documents and enforces the lease lifetime at the call boundary. Every candidate
// is rechecked against the live DB immediately before unlink, closing the scan-to-GC race.
[[nodiscard]] GarbageCollectionResult
collect_garbage(const VaultRoot& root, const WriterLock& writer_lock, const Database& database,
                std::span<const GarbageObject> candidates, uint64_t grace_seconds) noexcept;

}  // namespace vault::v3
