#include "vault/v3_recovery.h"

#include <algorithm>
#include <chrono>
#include <map>

namespace vault::v3 {

ReconciliationReport reconcile_objects(const VaultRoot& root,
                                        const Database& database) noexcept
{
    ReconciliationReport report;
    report.status = RecoveryStatus::Ok;
    const auto references = database.object_references();
    if (references.status != DbStatus::Ok) {
        report.status = RecoveryStatus::DatabaseError;
        return report;
    }
    const auto entries = root.list_objects();
    if (!entries) {
        report.status = RecoveryStatus::FilesystemError;
        return report;
    }

    std::map<ObjectId, ObjectEntry> physical;
    for (const auto& entry : *entries) {
        if (!physical.emplace(entry.id, entry).second) {
            report.status = RecoveryStatus::Corrupt;
            return report;
        }
    }
    for (const auto& reference : references.value) {
        const auto found = physical.find(reference.object_id);
        if (found == physical.end()) {
            report.missing.push_back(reference.object_id);
            continue;
        }
        if (found->second.size != reference.encrypted_length)
            report.size_mismatches.push_back(reference.object_id);
        else
            report.referenced_bytes += found->second.size;
        physical.erase(found);
    }
    for (const auto& [id, entry] : physical) {
        report.garbage.push_back({id, entry.size, entry.modified_seconds});
        report.garbage_bytes += entry.size;
    }
    return report;
}

GarbageCollectionResult collect_garbage(const VaultRoot& root, const WriterLock&,
                                        const Database& database,
                                        std::span<const GarbageObject> candidates,
                                        uint64_t grace_seconds) noexcept
{
    GarbageCollectionResult result{.status = RecoveryStatus::Ok};
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    for (const auto& candidate : candidates) {
        if (candidate.modified_seconds > now ||
            static_cast<uint64_t>(now - candidate.modified_seconds) < grace_seconds)
            continue;
        const auto referenced = database.object_is_referenced(candidate.id);
        if (referenced.status != DbStatus::Ok) {
            result.status = RecoveryStatus::DatabaseError;
            return result;
        }
        if (referenced.value) continue;
        const auto file = root.open_object(candidate.id);
        if (!file) continue;
        const auto size = file->size();
        if (!size || *size != candidate.encrypted_length) continue;
        if (!root.unlink_object(candidate.id)) {
            result.status = RecoveryStatus::FilesystemError;
            return result;
        }
        ++result.deleted;
        result.reclaimed_bytes += candidate.encrypted_length;
    }
    return result;
}

}  // namespace vault::v3
