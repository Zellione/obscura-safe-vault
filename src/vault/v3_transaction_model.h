#pragma once

#include <cstdint>

namespace vault::v3 {

enum class MutationKind : uint8_t { Create, Replace, Delete };
enum class MutationStep : uint8_t { Initial, Staged, Published, DatabaseCommitted, Cleaned };

struct RecoveryModel {
    bool database_references_old = false;
    bool database_references_new = false;
    bool old_object_exists = false;
    bool new_object_exists = false;

    [[nodiscard]] constexpr bool has_missing_reference() const noexcept
    {
        return (database_references_old && !old_object_exists) ||
               (database_references_new && !new_object_exists);
    }

    [[nodiscard]] constexpr bool has_reclaimable_garbage() const noexcept
    {
        return (old_object_exists && !database_references_old) ||
               (new_object_exists && !database_references_new);
    }
};

[[nodiscard]] constexpr RecoveryModel recovery_model(MutationKind kind,
                                                     MutationStep step) noexcept
{
    if (kind == MutationKind::Create) {
        return {
            .database_references_new = step >= MutationStep::DatabaseCommitted,
            .new_object_exists = step >= MutationStep::Published,
        };
    }
    if (kind == MutationKind::Replace) {
        return {
            .database_references_old = step < MutationStep::DatabaseCommitted,
            .database_references_new = step >= MutationStep::DatabaseCommitted,
            .old_object_exists = step < MutationStep::Cleaned,
            .new_object_exists = step >= MutationStep::Published,
        };
    }
    return {
        .database_references_old = step < MutationStep::DatabaseCommitted,
        .old_object_exists = step < MutationStep::Cleaned,
    };
}

} // namespace vault::v3
