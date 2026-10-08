#pragma once

#include "vault/v3_db.h"
#include "vault/v3_fs.h"
#include "vault/v3_object_store.h"

#include <cstdint>
#include <span>
#include <vector>
#include "vault/op_progress.h"

namespace vault::v3 {

enum class RecoveryStatus : uint8_t { Ok, DatabaseError, FilesystemError, Corrupt, Cancelled };
enum class VerifyDepth : uint8_t { Quick, Deep };
enum class IntegrityFindingKind : uint8_t {
    MissingReferencedObject,
    ReferencedObjectSizeMismatch,
    UnauthenticObject,
    MalformedObject,
    UnreferencedGarbage,
    InvalidMediaReferences,
};

struct IntegrityFinding {
    IntegrityFindingKind kind = IntegrityFindingKind::MalformedObject;
    ObjectId object_id{};
};

struct VerificationReport {
    RecoveryStatus status = RecoveryStatus::FilesystemError;
    VerifyDepth depth = VerifyDepth::Quick;
    uint64_t referenced_bytes = 0;
    uint64_t authenticated_bytes = 0;
    uint64_t garbage_bytes = 0;
    size_t objects_checked = 0;
    size_t garbage_objects = 0;
    std::vector<IntegrityFinding> findings;

    [[nodiscard]] bool has_corruption() const noexcept;
};

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
    size_t staging_removed = 0;
    size_t staging_preserved = 0;
    size_t suspicious_staging = 0;
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

// Quick verification checks the encrypted database plus reference/file consistency. Deep
// verification additionally authenticates every referenced object's header, table and frame.
// Findings contain opaque IDs only; user metadata and plaintext never enter diagnostics.
[[nodiscard]] VerificationReport verify_vault(const VaultRoot& root, const Database& database,
                                              std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                                              const Id& vault_id, VerifyDepth depth,
                                              CancellationToken cancel = {}) noexcept;

}  // namespace vault::v3
