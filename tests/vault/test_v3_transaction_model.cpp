#include "test_framework.h"

#include "vault/v3_transaction_model.h"

#include <array>

TEST(v3_publication_protocol_never_commits_a_missing_object)
{
    constexpr std::array kinds{
        vault::v3::MutationKind::Create,
        vault::v3::MutationKind::Replace,
        vault::v3::MutationKind::Delete,
    };
    constexpr std::array steps{
        vault::v3::MutationStep::Initial,
        vault::v3::MutationStep::Staged,
        vault::v3::MutationStep::Published,
        vault::v3::MutationStep::DatabaseCommitted,
        vault::v3::MutationStep::Cleaned,
    };
    for (const auto kind : kinds) {
        for (const auto step : steps)
            CHECK_FALSE(vault::v3::recovery_model(kind, step).has_missing_reference());
    }
}
TEST(v3_publication_protocol_leaves_only_reclaimable_extra_objects)
{
    using enum vault::v3::MutationKind;
    using enum vault::v3::MutationStep;
    CHECK_TRUE(vault::v3::recovery_model(Create, Published).has_reclaimable_garbage());
    CHECK_TRUE(vault::v3::recovery_model(Replace, Published).has_reclaimable_garbage());
    CHECK_TRUE(vault::v3::recovery_model(Replace, DatabaseCommitted).has_reclaimable_garbage());
    CHECK_TRUE(vault::v3::recovery_model(Delete, DatabaseCommitted).has_reclaimable_garbage());
    CHECK_FALSE(vault::v3::recovery_model(Create, Cleaned).has_reclaimable_garbage());
    CHECK_FALSE(vault::v3::recovery_model(Replace, Cleaned).has_reclaimable_garbage());
    CHECK_FALSE(vault::v3::recovery_model(Delete, Cleaned).has_reclaimable_garbage());
}
