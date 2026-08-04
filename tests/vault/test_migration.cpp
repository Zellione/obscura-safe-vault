#include "test_framework.h"

#include <vector>

#include "vault/index.h"

TEST(migration_watermark_round_trips_at_v10)
{
    vault::IndexNode root;
    root.type = vault::IndexNode::Type::Gallery;

    vault::VaultSettings s = vault::VaultSettings::seeded();
    s.migrated_index_version = 7;
    s.migrated_probe_caps    = 3;

    std::vector<uint8_t> blob;
    vault::serialize_index(root, {}, s, blob);
    CHECK(!blob.empty());
    CHECK_EQ(blob[0], vault::INDEX_VERSION);
    CHECK_EQ(vault::INDEX_VERSION, 10);

    vault::IndexNode out;
    std::vector<vault::SavedSearch> searches;
    vault::VaultSettings got;
    REQUIRE(vault::deserialize_index(blob, out, searches, got));
    CHECK_EQ(got.migrated_index_version, 7);
    CHECK_EQ(got.migrated_probe_caps, 3);
}

TEST(migration_watermark_defaults_to_zero_when_unset)
{
    vault::IndexNode root;
    root.type = vault::IndexNode::Type::Gallery;

    std::vector<uint8_t> blob;
    vault::serialize_index(root, {}, vault::VaultSettings::seeded(), blob);

    vault::IndexNode out;
    std::vector<vault::SavedSearch> searches;
    vault::VaultSettings got;
    REQUIRE(vault::deserialize_index(blob, out, searches, got));
    CHECK_EQ(got.migrated_index_version, 0);
    CHECK_EQ(got.migrated_probe_caps, 0);
}

TEST(migration_watermark_rejects_future_version)
{
    // A blob claiming migration to an index version this build does not know
    // is malformed input, not something to clamp.
    vault::IndexNode root;
    root.type = vault::IndexNode::Type::Gallery;

    vault::VaultSettings s = vault::VaultSettings::seeded();
    std::vector<uint8_t> blob;
    vault::serialize_index(root, {}, s, blob);

    // The watermark is the last 3 bytes of the settings block, which is the
    // tail of the blob: [.. migrated_index_version u8][migrated_probe_caps u16].
    blob[blob.size() - 3] = static_cast<uint8_t>(vault::INDEX_VERSION + 1);

    vault::IndexNode out;
    std::vector<vault::SavedSearch> searches;
    vault::VaultSettings got;
    CHECK(!vault::deserialize_index(blob, out, searches, got));
}
