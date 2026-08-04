#include "test_framework.h"

#include <filesystem>
#include <string>
#include <vector>

#include "vault/index.h"
#include "vault/migration.h"
#include "vault/vault.h"

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

namespace fs = std::filesystem;

static const crypto::KdfParams kMigKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> mig_bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

static std::vector<uint8_t> mig_pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

namespace {
struct MigTempVault {
    fs::path path;
    explicit MigTempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_mig_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~MigTempVault()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
    [[nodiscard]] std::string str() const { return path.string(); }
};
} // namespace

TEST(migration_pending_true_for_fresh_zero_watermark)
{
    vault::VaultSettings s = vault::VaultSettings::seeded();
    CHECK(vault::migration_pending(s, 1));
}

TEST(migration_pending_false_once_stamped)
{
    vault::VaultSettings s = vault::stamp_migrated(vault::VaultSettings::seeded(), 1);
    CHECK_EQ(s.migrated_index_version, vault::MIGRATION_INDEX_VERSION);
    CHECK_EQ(s.migrated_probe_caps, 1);
    CHECK(!vault::migration_pending(s, 1));
}

TEST(migration_pending_true_again_when_probe_caps_advance)
{
    vault::VaultSettings s = vault::stamp_migrated(vault::VaultSettings::seeded(), 1);
    CHECK(!vault::migration_pending(s, 1));
    CHECK(vault::migration_pending(s, 2));   // a new codec landed
}

TEST(migration_scan_counts_nothing_for_a_freshly_written_vault)
{
    MigTempVault tv("scan_clean");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("a") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("a", mig_pattern(1000, 1), "one.png") == vault::VaultResult::Ok);

    // A PNG cannot animate, so it is not backfill work; import already probed it.
    const vault::MigrationScan scan = vault::scan_migration(v);
    CHECK_EQ(scan.videos, 0u);
    CHECK_EQ(scan.images, 0u);
    CHECK(scan.empty());
}

TEST(migration_scan_walks_nested_galleries)
{
    MigTempVault tv("scan_nested");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("a") == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("a/b") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("a/b", mig_pattern(1000, 1), "deep.png") == vault::VaultResult::Ok);

    // Nothing to migrate, but the walk must not throw or miss the nesting.
    const vault::MigrationScan scan = vault::scan_migration(v);
    CHECK(scan.empty());
    CHECK_EQ(scan.bytes, 0u);
}
