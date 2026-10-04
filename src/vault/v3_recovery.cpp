#include "vault/v3_recovery.h"

#include <algorithm>
#include <chrono>
#include <map>

namespace vault::v3 {

bool VerificationReport::has_corruption() const noexcept
{
    return std::ranges::any_of(findings, [](const IntegrityFinding& finding) {
        return finding.kind != IntegrityFindingKind::UnreferencedGarbage;
    });
}

ReconciliationReport reconcile_objects(const VaultRoot& root, const Database& database) noexcept
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
        if (!physical.try_emplace(entry.id, entry).second) {
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
        report.garbage.emplace_back(id, entry.size, entry.modified_seconds);
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
        if (const auto size = file->size(); !size || *size != candidate.encrypted_length) continue;
        if (!root.unlink_object(candidate.id)) {
            result.status = RecoveryStatus::FilesystemError;
            return result;
        }
        ++result.deleted;
        result.reclaimed_bytes += candidate.encrypted_length;
    }
    return result;
}

namespace {
uint8_t media_format_for(const Database& database, const ObjectRecord& object, bool& valid) noexcept
{
    const auto node = database.find_node(object.node_id);
    if (node.status != DbStatus::Ok || !node.value.has_value() ||
        !node.value->media_format.has_value() || *node.value->media_format < 0 ||
        *node.value->media_format > 255) {
        valid = false;
        return 0;
    }
    valid = true;
    return static_cast<uint8_t>(*node.value->media_format);
}

IntegrityFindingKind finding_for(ObjectStatus status) noexcept
{
    return status == ObjectStatus::AuthenticationFailed ? IntegrityFindingKind::UnauthenticObject
                                                        : IntegrityFindingKind::MalformedObject;
}

bool is_unreadable(const ReconciliationReport& reconciled, const ObjectId& id) noexcept
{
    return std::ranges::find(reconciled.missing, id) != reconciled.missing.end() ||
           std::ranges::find(reconciled.size_mismatches, id) != reconciled.size_mismatches.end();
}

void authenticate_object(const VaultRoot& root, const Database& database,
                         std::span<const uint8_t, crypto::KEY_SIZE> master_key, const Id& vault_id,
                         const ObjectRecord& object, VerificationReport& report) noexcept
{
    bool valid_format = false;
    const auto media_format = media_format_for(database, object, valid_format);
    if (!valid_format) {
        report.findings.push_back({IntegrityFindingKind::MalformedObject, object.object_id});
        return;
    }
    const ObjectInfo expected{object.object_id,
                              vault_id,
                              object.node_id,
                              object.role,
                              media_format,
                              object.plaintext_length,
                              object.encrypted_length,
                              object.frame_plain_limit,
                              object.frame_count};
    auto opened = ObjectReader::open(root, master_key, expected);
    if (opened.status != ObjectStatus::Ok || !opened.reader) {
        report.findings.push_back({finding_for(opened.status), object.object_id});
        return;
    }
    for (uint32_t frame = 0; frame < object.frame_count; ++frame) {
        crypto::SecureBytes plaintext;
        const auto status = opened.reader->read_frame(frame, plaintext);
        if (status != ObjectStatus::Ok) {
            report.findings.push_back({finding_for(status), object.object_id});
            return;
        }
        report.authenticated_bytes += plaintext.size();
    }
    ++report.objects_checked;
}

bool authenticate_references(const VaultRoot& root, const Database& database,
                             std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                             const Id& vault_id, const ReconciliationReport& reconciled,
                             VerificationReport& report) noexcept
{
    const auto references = database.object_references();
    if (references.status != DbStatus::Ok) return false;
    for (const auto& object : references.value) {
        if (is_unreadable(reconciled, object.object_id)) continue;
        authenticate_object(root, database, master_key, vault_id, object, report);
    }
    return true;
}
}  // namespace

VerificationReport verify_vault(const VaultRoot& root,
                                const Database& database,  // NOSONAR cpp:S3776
                                std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                                const Id& vault_id, VerifyDepth depth) noexcept
{
    VerificationReport report;
    report.status = RecoveryStatus::Ok;
    report.depth = depth;
    if (const bool database_ok = depth == VerifyDepth::Deep ? database_deep_healthy(database)
                                                            : database_healthy(database);
        !database_ok) {
        report.status = RecoveryStatus::DatabaseError;
        return report;
    }
    const auto reconciled = reconcile_objects(root, database);
    if (reconciled.status != RecoveryStatus::Ok) {
        report.status = reconciled.status;
        return report;
    }
    report.referenced_bytes = reconciled.referenced_bytes;
    report.garbage_bytes = reconciled.garbage_bytes;
    report.garbage_objects = reconciled.garbage.size();
    for (const auto& id : reconciled.missing)
        report.findings.push_back({IntegrityFindingKind::MissingReferencedObject, id});
    for (const auto& id : reconciled.size_mismatches)
        report.findings.push_back({IntegrityFindingKind::ReferencedObjectSizeMismatch, id});
    for (const auto& garbage : reconciled.garbage)
        report.findings.push_back({IntegrityFindingKind::UnreferencedGarbage, garbage.id});

    if (depth == VerifyDepth::Deep &&
        !authenticate_references(root, database, master_key, vault_id, reconciled, report)) {
        report.status = RecoveryStatus::DatabaseError;
        return report;
    }
    if (report.has_corruption()) report.status = RecoveryStatus::Corrupt;
    return report;
}

}  // namespace vault::v3
