#include "test_framework.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "image/fixtures.h"
#include "vault/index.h"
#include "vault/migration.h"
#include "vault/vault.h"

namespace vault {
// Test-only seams defined in tests/vault/test_video.cpp (linked into osv_tests).
// Forward declarations for cross-translation-unit use.
void test_only_force_video_codec_unknown(Vault& v, std::string_view node_path);
void test_only_force_image_animated_unknown(Vault& v, std::string_view node_path);
}  // namespace vault

// Read a fixture file into a vector.
static std::vector<uint8_t> read_file(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

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

TEST(migration_scan_counts_animatable_images_backfill)
{
    // Image arm positive case: an animatable format (WebP) marked as
    // un-backfilled (animated = false) is counted as work.
    MigTempVault tv("scan_anim_backfill");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("img") == vault::VaultResult::Ok);

    auto anim_webp = fixtures::load_anim_webp();
    REQUIRE(!anim_webp.empty());
    REQUIRE(v.add_image("img", anim_webp, "anim.webp") == vault::VaultResult::Ok);

    // At this point, anim.webp was detected and meta.animated is true.
    // Simulate an old vault by forcing it to false (standing in for
    // "imported before animation detection existed").
    vault::test_only_force_image_animated_unknown(v, "img/anim.webp");

    // Now the scan should count this as backfill work.
    const vault::MigrationScan scan = vault::scan_migration(v);
    CHECK_EQ(scan.images, 1u);
    CHECK_EQ(scan.videos, 0u);
    CHECK_EQ(scan.bytes, static_cast<uint64_t>(anim_webp.size()));
}

TEST(migration_scan_excludes_non_animatable_images)
{
    // Image arm negative case: PNG is not animatable, so even if a PNG is
    // present, it should not be counted as work.
    MigTempVault tv("scan_no_png_backfill");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("img") == vault::VaultResult::Ok);

    auto png = fixtures::solid_png(8, 8, 255, 0, 0);
    REQUIRE(!png.empty());
    REQUIRE(v.add_image("img", png, "static.png") == vault::VaultResult::Ok);

    // PNG is not an animatable format, so it should never be counted regardless
    // of the animated flag state.
    const vault::MigrationScan scan = vault::scan_migration(v);
    CHECK_EQ(scan.images, 0u);
    CHECK(scan.empty());
}

#ifdef OSV_VENDORED_AV
TEST(migration_scan_counts_unknown_codec_videos)
{
    // Video arm positive case: a video with codec == Unknown is counted as work.
    MigTempVault tv("scan_video_unknown");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);

    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video_bytes.empty());
    REQUIRE(v.add_video("", video_bytes, "tiny.mp4", 4096) == vault::VaultResult::Ok);

    // At this point, tiny.mp4 was probed and codec is known. Simulate an old
    // vault by forcing codec to Unknown (standing in for "imported before this
    // build's FFmpeg could decode it").
    vault::test_only_force_video_codec_unknown(v, "tiny.mp4");

    // Now the scan should count this as backfill work.
    const vault::MigrationScan scan = vault::scan_migration(v);
    CHECK_EQ(scan.videos, 1u);
    CHECK_EQ(scan.images, 0u);
    CHECK_EQ(scan.bytes, static_cast<uint64_t>(video_bytes.size()));
}

TEST(migration_scan_byte_accounting_mixed)
{
    // Byte accounting over mixed content: 1 animatable image backfill +
    // 1 unknown-codec video + 1 static PNG (not counted).
    MigTempVault tv("scan_mixed_bytes");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("mixed") == vault::VaultResult::Ok);

    auto anim_webp = fixtures::load_anim_webp();
    auto png = fixtures::solid_png(8, 8, 0, 255, 0);
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!anim_webp.empty() && !png.empty() && !video_bytes.empty());

    REQUIRE(v.add_image("mixed", anim_webp, "anim.webp") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("mixed", png, "static.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("mixed", video_bytes, "tiny.mp4", 4096) == vault::VaultResult::Ok);

    vault::test_only_force_image_animated_unknown(v, "mixed/anim.webp");
    vault::test_only_force_video_codec_unknown(v, "mixed/tiny.mp4");

    const vault::MigrationScan scan = vault::scan_migration(v);
    CHECK_EQ(scan.images, 1u);
    CHECK_EQ(scan.videos, 1u);
    CHECK_EQ(scan.total(), 2u);
    CHECK_EQ(scan.bytes, static_cast<uint64_t>(anim_webp.size() + video_bytes.size()));
}
#endif  // OSV_VENDORED_AV

TEST(apply_image_animated_defers_the_commit)
{
    MigTempVault tv("apply_anim");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", mig_pattern(1000, 1), "a.png") == vault::VaultResult::Ok);

    // A PNG cannot animate: the apply is a well-formed no-op, not an error.
    CHECK(vault::apply_image_animated(v, "a.png", true) == vault::VaultResult::Ok);

    CHECK(vault::apply_image_animated(v, "missing.png", true)
          == vault::VaultResult::NotFound);
}

TEST(commit_migration_persists_the_watermark)
{
    MigTempVault tv("commit_wm");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
                == vault::VaultResult::Ok);
        CHECK(vault::migration_pending(vault::vault_settings(v), 1));

        const vault::VaultSettings stamped =
            vault::stamp_migrated(vault::vault_settings(v), 1);
        REQUIRE(vault::commit_migration(v, stamped) == vault::VaultResult::Ok);
        CHECK(!vault::migration_pending(vault::vault_settings(v), 1));
    }
    // Survives a close/reopen — this is the whole point of the watermark.
    {
        vault::Vault v;
        REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
        REQUIRE(v.unlock(mig_bytes("pw"), {}) == vault::VaultResult::Ok);
        CHECK(!vault::migration_pending(vault::vault_settings(v), 1));
        CHECK(vault::migration_pending(vault::vault_settings(v), 2));
    }
}
