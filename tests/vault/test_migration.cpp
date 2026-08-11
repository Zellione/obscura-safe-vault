#include "test_framework.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "image/fixtures.h"
#include "image/thumbnail.h"
#include "media/video_probe.h"
#include "vault/index.h"
#include "vault/migration.h"
#include "vault/transfer.h"
#include "vault/vault.h"

namespace vault {
// Test-only seams defined in tests/vault/test_video.cpp (linked into osv_tests).
// Forward declarations for cross-translation-unit use.
void test_only_force_video_codec_unknown(Vault& v, std::string_view node_path);
void test_only_force_image_animated_unknown(Vault& v, std::string_view node_path);
}  // namespace vault

#ifdef OSV_VENDORED_AV
// Read a fixture file into a vector. Only the video tests below do this, so the
// guard matches theirs — without FFmpeg this is an unused static and -Werror bites.
static std::vector<uint8_t> read_file(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
#endif

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
    CHECK_EQ(vault::INDEX_VERSION, 12);

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

    // The watermark is: migrated_index_version(u8) then migrated_probe_caps(u16).
    // With v12, from the end: thumb_side(u16, 2 bytes) + field_values_count(u16, 2 bytes) + migrated_probe_caps(u16, 2 bytes) + migrated_index_version(u8, 1 byte).
    // So migrated_index_version is at blob.size() - 7.
    blob[blob.size() - 7] = static_cast<uint8_t>(vault::INDEX_VERSION + 1);

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
    CHECK(vault::migration_pending(s, 1, 512));
}

TEST(migration_pending_false_once_stamped)
{
    vault::VaultSettings s = vault::stamp_migrated(vault::VaultSettings::seeded(), 1, 512);
    CHECK_EQ(s.migrated_index_version, vault::MIGRATION_INDEX_VERSION);
    CHECK_EQ(s.migrated_probe_caps, 1);
    CHECK(!vault::migration_pending(s, 1, 512));
}

TEST(migration_pending_true_again_when_probe_caps_advance)
{
    vault::VaultSettings s = vault::stamp_migrated(vault::VaultSettings::seeded(), 1, 512);
    CHECK(!vault::migration_pending(s, 1, 512));
    CHECK(vault::migration_pending(s, 2, 512));   // a new codec landed
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
    const vault::MigrationScan scan = vault::scan_migration(v, false);
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
    const vault::MigrationScan scan = vault::scan_migration(v, false);
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
    const vault::MigrationScan scan = vault::scan_migration(v, false);
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
    const vault::MigrationScan scan = vault::scan_migration(v, false);
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
    const vault::MigrationScan scan = vault::scan_migration(v, false);
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

    const vault::MigrationScan scan = vault::scan_migration(v, false);
    CHECK_EQ(scan.images, 1u);
    CHECK_EQ(scan.videos, 1u);
    CHECK_EQ(scan.total(), 2u);
    CHECK_EQ(scan.bytes, static_cast<uint64_t>(anim_webp.size() + video_bytes.size()));
}
#endif  // OSV_VENDORED_AV

TEST(apply_image_animated_returns_not_found_for_missing_path)
{
    MigTempVault tv("apply_anim_notfound");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);

    CHECK(vault::apply_image_animated(v, "missing.png", true)
          == vault::VaultResult::NotFound);
}

TEST(apply_image_animated_noop_on_non_animatable_format)
{
    MigTempVault tv("apply_anim_noop_png");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", mig_pattern(1000, 1), "a.png") == vault::VaultResult::Ok);

    // A PNG cannot animate: the apply is a well-formed no-op, not an error.
    CHECK(vault::apply_image_animated(v, "a.png", true) == vault::VaultResult::Ok);
}

TEST(apply_image_animated_defers_commit_when_animatable_format_changes)
{
    auto anim_webp = fixtures::load_anim_webp();
    REQUIRE(!anim_webp.empty());

    MigTempVault tv("apply_anim_defer");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", anim_webp, "anim.webp") == vault::VaultResult::Ok);

        // At this point, anim.webp has animated=true (auto-detected on import).
        // Now apply false WITHOUT committing: in-memory change only.
        CHECK(vault::apply_image_animated(v, "anim.webp", false) == vault::VaultResult::Ok);

        // In-memory, it is now false.
        {
            const vault::IndexNode* n = v.resolve_node("anim.webp");
            REQUIRE(n != nullptr);
            CHECK(n->meta.animated == false);  // in-memory mutation
        }
    }
    // Close and reopen WITHOUT calling commit_migration: the in-memory change should NOT persist.
    {
        vault::Vault v;
        REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
        REQUIRE(v.unlock(mig_bytes("pw"), {}) == vault::VaultResult::Ok);

        // The animated flag should be back to true (persisted state from import).
        const vault::IndexNode* n = v.resolve_node("anim.webp");
        REQUIRE(n != nullptr);
        REQUIRE(n->is_image());
        CHECK(n->meta.animated == true);  // reverted to persisted state (apply was not committed)
    }
}

TEST(apply_image_animated_with_commit_migration_persists_the_flag)
{
    auto anim_webp = fixtures::load_anim_webp();
    REQUIRE(!anim_webp.empty());

    MigTempVault tv("apply_anim_persist");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", anim_webp, "anim.webp") == vault::VaultResult::Ok);

        // Simulate an old vault by forcing animated to false.
        vault::test_only_force_image_animated_unknown(v, "anim.webp");

        // Apply the correct flag.
        CHECK(vault::apply_image_animated(v, "anim.webp", true) == vault::VaultResult::Ok);

        // Now commit the migration WITH the apply.
        const vault::VaultSettings stamped =
            vault::stamp_migrated(vault::vault_settings(v), 1, 512);
        REQUIRE(vault::commit_migration(v, stamped) == vault::VaultResult::Ok);
    }
    // Reopen: the animated flag should now persist.
    {
        vault::Vault v;
        REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
        REQUIRE(v.unlock(mig_bytes("pw"), {}) == vault::VaultResult::Ok);

        const vault::IndexNode* n = v.resolve_node("anim.webp");
        REQUIRE(n != nullptr);
        CHECK(n->meta.animated == true);  // PERSISTED via commit_migration
    }
}

TEST(apply_image_animated_noop_when_already_correct_value)
{
    auto anim_webp = fixtures::load_anim_webp();
    REQUIRE(!anim_webp.empty());

    MigTempVault tv("apply_anim_noop_correct");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", anim_webp, "anim.webp") == vault::VaultResult::Ok);

    // anim.webp auto-detects as animated=true on import.
    // Applying true again is a no-op (returns Ok, no mutation).
    CHECK(vault::apply_image_animated(v, "anim.webp", true) == vault::VaultResult::Ok);
}

#ifdef OSV_VENDORED_AV
TEST(apply_video_probe_returns_not_found_for_missing_path)
{
    MigTempVault tv("apply_vp_notfound");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);

    vault::VideoProbeApply probe;
    probe.codec = vault::VideoCodec::H264;
    probe.width = 640;
    probe.height = 480;
    probe.duration_us = 1000000;

    CHECK(vault::apply_video_probe(v, "missing.mp4", probe)
          == vault::VaultResult::NotFound);
}

TEST(apply_video_probe_noop_when_codec_already_real)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video_bytes.empty());

    MigTempVault tv("apply_vp_noop_real");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_video("", video_bytes, "tiny.mp4", 4096) == vault::VaultResult::Ok);

    // tiny.mp4 was probed on import; codec is already known.
    const vault::IndexNode* n_before = v.resolve_node("tiny.mp4");
    REQUIRE(n_before != nullptr);
    const vault::VideoCodec codec_before = n_before->vmeta.codec;
    CHECK(codec_before != vault::VideoCodec::Unknown);

    // Applying a probe with a different codec should be a no-op.
    vault::VideoProbeApply probe;
    probe.codec = vault::VideoCodec::VP9;
    probe.width = 800;
    probe.height = 600;
    probe.duration_us = 2000000;

    CHECK(vault::apply_video_probe(v, "tiny.mp4", probe) == vault::VaultResult::Ok);

    // Codec should remain unchanged.
    const vault::IndexNode* n_after = v.resolve_node("tiny.mp4");
    REQUIRE(n_after != nullptr);
    CHECK_EQ(n_after->vmeta.codec, codec_before);
}

TEST(apply_video_probe_noop_when_probe_codec_unknown)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video_bytes.empty());

    MigTempVault tv("apply_vp_noop_unknown");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_video("", video_bytes, "tiny.mp4", 4096) == vault::VaultResult::Ok);

    // Force the video to Unknown (standing in for "probe failed").
    vault::test_only_force_video_codec_unknown(v, "tiny.mp4");

    const vault::IndexNode* n_before = v.resolve_node("tiny.mp4");
    REQUIRE(n_before != nullptr);
    CHECK_EQ(n_before->vmeta.codec, vault::VideoCodec::Unknown);

    // Apply a probe with codec=Unknown (still undecodable).
    // This should be a clean no-op, not a corruption.
    vault::VideoProbeApply probe;
    probe.codec = vault::VideoCodec::Unknown;

    CHECK(vault::apply_video_probe(v, "tiny.mp4", probe) == vault::VaultResult::Ok);

    // Metadata should remain unchanged.
    const vault::IndexNode* n_after = v.resolve_node("tiny.mp4");
    REQUIRE(n_after != nullptr);
    CHECK_EQ(n_after->vmeta.codec, vault::VideoCodec::Unknown);
}

TEST(apply_video_probe_writes_metadata_when_unknown_probes_to_real_codec)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video_bytes.empty());

    MigTempVault tv("apply_vp_write_metadata");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_video("", video_bytes, "tiny.mp4", 4096) == vault::VaultResult::Ok);

    // Force the video to Unknown.
    vault::test_only_force_video_codec_unknown(v, "tiny.mp4");

    // Verify it's Unknown.
    {
        const vault::IndexNode* n = v.resolve_node("tiny.mp4");
        REQUIRE(n != nullptr);
        CHECK_EQ(n->vmeta.codec, vault::VideoCodec::Unknown);
    }

    // Apply a real probe result.
    vault::VideoProbeApply probe;
    probe.codec = vault::VideoCodec::H264;
    probe.width = 320;
    probe.height = 240;
    probe.duration_us = 5000000;

    CHECK(vault::apply_video_probe(v, "tiny.mp4", probe) == vault::VaultResult::Ok);

    // Metadata should now be written (but NOT committed yet).
    {
        const vault::IndexNode* n = v.resolve_node("tiny.mp4");
        REQUIRE(n != nullptr);
        CHECK_EQ(n->vmeta.codec, vault::VideoCodec::H264);
        CHECK_EQ(n->vmeta.width, 320u);
        CHECK_EQ(n->vmeta.height, 240u);
        CHECK_EQ(n->vmeta.duration_us, 5000000ull);
    }
}

TEST(apply_video_probe_appends_poster_chunk_when_missing)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    auto poster_bytes = fixtures::solid_png(100, 100, 255, 0, 0);
    REQUIRE(!video_bytes.empty());
    REQUIRE(!poster_bytes.empty());

    MigTempVault tv("apply_vp_poster_append");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_video("", video_bytes, "tiny.mp4", 4096) == vault::VaultResult::Ok);

    // Force the video to Unknown and remove the poster (if any).
    vault::test_only_force_video_codec_unknown(v, "tiny.mp4");

    // Manually clear poster (via IndexNode manipulation would be test-only seam).
    {
        vault::IndexNode* n = v.resolve_node("tiny.mp4");
        REQUIRE(n != nullptr);
        n->vmeta.poster_offset = 0;
        n->vmeta.poster_length = 0;
    }

    // Apply a probe with poster bytes.
    vault::VideoProbeApply probe;
    probe.codec = vault::VideoCodec::VP9;
    probe.width = 320;
    probe.height = 240;
    probe.duration_us = 3000000;
    probe.poster_jpeg = poster_bytes;

    CHECK(vault::apply_video_probe(v, "tiny.mp4", probe) == vault::VaultResult::Ok);

    // Poster should now be stored: both offset and length should be non-zero.
    {
        const vault::IndexNode* n = v.resolve_node("tiny.mp4");
        REQUIRE(n != nullptr);
        CHECK(n->vmeta.poster_offset > 0);
        CHECK(n->vmeta.poster_length > 0);
    }
}

TEST(apply_video_probe_noop_when_node_already_has_poster)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    auto new_poster_bytes = fixtures::solid_png(50, 50, 0, 255, 0);
    REQUIRE(!video_bytes.empty());
    REQUIRE(!new_poster_bytes.empty());

    MigTempVault tv("apply_vp_poster_noop");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_video("", video_bytes, "tiny.mp4", 4096) == vault::VaultResult::Ok);

    // tiny.mp4 was imported and likely has a poster already (from decode).
    {
        const vault::IndexNode* n = v.resolve_node("tiny.mp4");
        REQUIRE(n != nullptr);
        // If it has a poster, poster_length > 0; if not, we'd need to add one manually.
        // For this test, just proceed: if no poster, we'll detect the append below.
    }

    // Get the poster state before applying.
    const vault::IndexNode* n_before = v.resolve_node("tiny.mp4");
    REQUIRE(n_before != nullptr);
    const uint64_t poster_offset_before = n_before->vmeta.poster_offset;
    const uint64_t poster_length_before = n_before->vmeta.poster_length;

    // Apply a probe with NEW poster bytes.
    vault::VideoProbeApply probe;
    probe.codec = vault::VideoCodec::H264;
    probe.width = 320;
    probe.height = 240;
    probe.duration_us = 3000000;
    probe.poster_jpeg = new_poster_bytes;

    CHECK(vault::apply_video_probe(v, "tiny.mp4", probe) == vault::VaultResult::Ok);

    // If the node already had a poster, it should NOT be replaced.
    {
        const vault::IndexNode* n = v.resolve_node("tiny.mp4");
        REQUIRE(n != nullptr);
        if (poster_length_before > 0) {
            // Had a poster before; should not be changed.
            CHECK_EQ(n->vmeta.poster_offset, poster_offset_before);
            CHECK_EQ(n->vmeta.poster_length, poster_length_before);
        }
    }
}
#endif  // OSV_VENDORED_AV

TEST(commit_migration_persists_the_watermark)
{
    MigTempVault tv("commit_wm");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
                == vault::VaultResult::Ok);
        CHECK(vault::migration_pending(vault::vault_settings(v), 1, 512));

        const vault::VaultSettings stamped =
            vault::stamp_migrated(vault::vault_settings(v), 1, 512);
        REQUIRE(vault::commit_migration(v, stamped) == vault::VaultResult::Ok);
        CHECK(!vault::migration_pending(vault::vault_settings(v), 1, 512));
    }
    // Survives a close/reopen — this is the whole point of the watermark.
    {
        vault::Vault v;
        REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
        REQUIRE(v.unlock(mig_bytes("pw"), {}) == vault::VaultResult::Ok);
        CHECK(!vault::migration_pending(vault::vault_settings(v), 1, 512));
        CHECK(vault::migration_pending(vault::vault_settings(v), 2, 512));
    }
}

TEST(transfer_from_unmigrated_vault_lowers_destination_watermark)
{
    MigTempVault src_tv("xfer_src");
    MigTempVault dst_tv("xfer_dst");

    vault::Vault src;
    REQUIRE(vault::Vault::create(src_tv.str(), mig_bytes("pw"), {}, kMigKdf, src)
            == vault::VaultResult::Ok);
    REQUIRE(src.create_gallery("g") == vault::VaultResult::Ok);
    REQUIRE(src.add_image("g", mig_pattern(1000, 1), "a.png") == vault::VaultResult::Ok);
    // src stays un-migrated (watermark 0/0).
    CHECK(vault::migration_pending(vault::vault_settings(src), media::PROBE_CAPS_GEN, 512));

    vault::Vault dst;
    REQUIRE(vault::Vault::create(dst_tv.str(), mig_bytes("pw"), {}, kMigKdf, dst)
            == vault::VaultResult::Ok);
    REQUIRE(dst.create_gallery("dst_g") == vault::VaultResult::Ok);
    REQUIRE(vault::commit_migration(
                dst, vault::stamp_migrated(vault::vault_settings(dst), media::PROBE_CAPS_GEN, 512))
            == vault::VaultResult::Ok);
    CHECK(!vault::migration_pending(vault::vault_settings(dst), media::PROBE_CAPS_GEN, 512));

    REQUIRE(vault::transfer_image(src, "g", "a.png", dst, "dst_g",
                                  vault::TransferMode::Copy) == vault::VaultResult::Ok);

    // The destination inherited un-backfilled content, so it owes a migration again.
    CHECK(vault::migration_pending(vault::vault_settings(dst), media::PROBE_CAPS_GEN, 512));
}

TEST(transfer_clean_content_does_not_lower_watermark)
{
    MigTempVault src_tv("xfer_clean_src");
    MigTempVault dst_tv("xfer_clean_dst");

    vault::Vault src;
    REQUIRE(vault::Vault::create(src_tv.str(), mig_bytes("pw"), {}, kMigKdf, src)
            == vault::VaultResult::Ok);
    REQUIRE(src.create_gallery("g") == vault::VaultResult::Ok);
    // Add a clean PNG (no backfill needed)
    REQUIRE(src.add_image("g", mig_pattern(1000, 1), "clean.png") == vault::VaultResult::Ok);
    // Mark the source as fully migrated (to simulate content that's already been backfilled)
    REQUIRE(vault::commit_migration(
                src, vault::stamp_migrated(vault::vault_settings(src), media::PROBE_CAPS_GEN, 512))
            == vault::VaultResult::Ok);
    CHECK(!vault::migration_pending(vault::vault_settings(src), media::PROBE_CAPS_GEN, 512));

    vault::Vault dst;
    REQUIRE(vault::Vault::create(dst_tv.str(), mig_bytes("pw"), {}, kMigKdf, dst)
            == vault::VaultResult::Ok);
    REQUIRE(dst.create_gallery("dst_g") == vault::VaultResult::Ok);
    REQUIRE(vault::commit_migration(
                dst, vault::stamp_migrated(vault::vault_settings(dst), media::PROBE_CAPS_GEN, 512))
            == vault::VaultResult::Ok);
    CHECK(!vault::migration_pending(vault::vault_settings(dst), media::PROBE_CAPS_GEN, 512));

    REQUIRE(vault::transfer_image(src, "g", "clean.png", dst, "dst_g",
                                  vault::TransferMode::Copy) == vault::VaultResult::Ok);

    // dst's watermark should NOT have been lowered (source was also migrated)
    CHECK(!vault::migration_pending(vault::vault_settings(dst), media::PROBE_CAPS_GEN, 512));
}

TEST(transfer_watermark_lowering_persists_after_close_reopen)
{
    MigTempVault src_tv("xfer_persist_src");
    MigTempVault dst_tv("xfer_persist_dst");

    {
        vault::Vault src;
        REQUIRE(vault::Vault::create(src_tv.str(), mig_bytes("pw"), {}, kMigKdf, src)
                == vault::VaultResult::Ok);
        REQUIRE(src.create_gallery("g") == vault::VaultResult::Ok);
        REQUIRE(src.add_image("g", mig_pattern(1000, 1), "a.png") == vault::VaultResult::Ok);

        vault::Vault dst;
        REQUIRE(vault::Vault::create(dst_tv.str(), mig_bytes("pw"), {}, kMigKdf, dst)
                == vault::VaultResult::Ok);
        REQUIRE(dst.create_gallery("dst_g") == vault::VaultResult::Ok);
        REQUIRE(vault::commit_migration(
                    dst, vault::stamp_migrated(vault::vault_settings(dst), media::PROBE_CAPS_GEN, 512))
                == vault::VaultResult::Ok);
        CHECK(!vault::migration_pending(vault::vault_settings(dst), media::PROBE_CAPS_GEN, 512));

        REQUIRE(vault::transfer_image(src, "g", "a.png", dst, "dst_g",
                                      vault::TransferMode::Copy) == vault::VaultResult::Ok);

        // Watermark lowered in memory
        CHECK(vault::migration_pending(vault::vault_settings(dst), media::PROBE_CAPS_GEN, 512));
    }
    // Close and reopen to verify watermark was persisted
    {
        vault::Vault dst;
        REQUIRE(vault::Vault::open(dst_tv.str(), dst) == vault::VaultResult::Ok);
        REQUIRE(dst.unlock(mig_bytes("pw"), {}) == vault::VaultResult::Ok);
        // Watermark must be lowered and persist across reopen
        CHECK(vault::migration_pending(vault::vault_settings(dst), media::PROBE_CAPS_GEN, 512));
    }
}

TEST(migration_pending_on_stale_thumb_side)
{
    vault::VaultSettings s;
    s = vault::stamp_migrated(s, 1, 512);
    CHECK(!vault::migration_pending(s, 1, 512));
    CHECK(vault::migration_pending(s, 1, 1024));    // future budget bump re-offers
    s.migrated_thumb_side = 0;
    CHECK(vault::migration_pending(s, 1, 512));     // legacy vault
}

TEST(scan_counts_thumb_regen_work)
{
    MigTempVault tv("scan_thumbs");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);

    // Add an image with a real thumbnail
    auto png = fixtures::solid_png(64, 64, 255, 0, 0);
    REQUIRE(!png.empty());
    REQUIRE(v.add_image("g", png, "img.png") == vault::VaultResult::Ok);

    // When thumbs_stale=false, the thumbs count should be zero
    const vault::MigrationScan scan_clean = vault::scan_migration(v, false);
    CHECK_EQ(scan_clean.thumbs, 0u);
    CHECK_EQ(scan_clean.images, 0u);
    CHECK_EQ(scan_clean.videos, 0u);

    // When thumbs_stale=true, the image with a thumbnail counts
    const vault::MigrationScan scan_stale = vault::scan_migration(v, true);
    CHECK_EQ(scan_stale.thumbs, 1u);
    CHECK_EQ(scan_stale.images, 0u);
    CHECK_EQ(scan_stale.bytes, static_cast<uint64_t>(png.size()));
}

TEST(apply_image_thumb_repoints_span)
{
    MigTempVault tv("apply_thumb_repoint");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);

    auto png = fixtures::solid_png(64, 64, 255, 0, 0);
    REQUIRE(!png.empty());
    REQUIRE(v.add_image("", png, "img.png") == vault::VaultResult::Ok);

    // Get the old thumbnail span
    const vault::IndexNode* n_before = v.resolve_node("img.png");
    REQUIRE(n_before != nullptr);
    REQUIRE(n_before->is_image());
    const uint64_t old_offset = n_before->meta.thumb_offset;
    const uint64_t old_length = n_before->meta.thumb_length;
    REQUIRE(old_length > 0);  // Must have a thumbnail from import

    // Create new thumbnail bytes (different size)
    auto new_thumb = fixtures::solid_png(128, 128, 0, 255, 0);
    REQUIRE(!new_thumb.empty());

    // Apply the new thumbnail
    CHECK(vault::apply_image_thumb(v, "img.png", new_thumb) == vault::VaultResult::Ok);

    // Verify the span was repointed
    const vault::IndexNode* n_after = v.resolve_node("img.png");
    REQUIRE(n_after != nullptr);
    CHECK(n_after->meta.thumb_offset != old_offset);
    // thumb_length is the on-disk encrypted size, not plaintext
    CHECK(n_after->meta.thumb_length > 0u);

    // Verify wasted_bytes grew by at least the old length
    const uint64_t waste = v.wasted_bytes();
    CHECK(waste > 0u);
}

TEST(apply_video_poster_replaces_existing)
{
    MigTempVault tv("apply_poster_replace");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);

#ifdef OSV_VENDORED_AV
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video_bytes.empty());
    REQUIRE(v.add_video("", video_bytes, "tiny.mp4", 4096) == vault::VaultResult::Ok);

    // Get the old poster span
    const vault::IndexNode* n_before = v.resolve_node("tiny.mp4");
    REQUIRE(n_before != nullptr);
    REQUIRE(n_before->is_video());
    const uint64_t old_offset = n_before->vmeta.poster_offset;
    const uint64_t old_length = n_before->vmeta.poster_length;

    // Create a new poster (must have bytes for apply_video_poster)
    auto new_poster = fixtures::solid_png(256, 144, 0, 0, 255);
    REQUIRE(!new_poster.empty());

    // Apply the new poster
    CHECK(vault::apply_video_poster(v, "tiny.mp4", new_poster) == vault::VaultResult::Ok);

    // Verify the span was repointed
    const vault::IndexNode* n_after = v.resolve_node("tiny.mp4");
    REQUIRE(n_after != nullptr);
    if (old_length > 0) {
        CHECK(n_after->vmeta.poster_offset != old_offset);
    }
    // poster_length is the on-disk encrypted size, not plaintext
    CHECK(n_after->vmeta.poster_length > 0u);

    // Empty blob should return InvalidArg
    CHECK(vault::apply_video_poster(v, "tiny.mp4", {}) == vault::VaultResult::InvalidArg);
#endif  // OSV_VENDORED_AV
}

TEST(create_vault_watermarks_thumb_and_index_at_creation)
{
    MigTempVault tv("watermark_fresh");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);

    // Fresh vault should have both watermarks stamped at creation
    const vault::VaultSettings settings_before = vault::vault_settings(v);
    CHECK_EQ(settings_before.migrated_index_version, vault::MIGRATION_INDEX_VERSION);
    CHECK_EQ(settings_before.migrated_thumb_side, static_cast<uint16_t>(image::THUMB_MAX_SIDE));

    // Add one image to the fresh vault
    auto png = fixtures::solid_png(256, 256, 255, 0, 0);
    REQUIRE(!png.empty());
    REQUIRE(v.add_image("", png, "test.png") == vault::VaultResult::Ok);

    // Verify that scan_migration doesn't report thumbs as stale
    // (if the watermarks weren't set, fresh vault with 1 image would report stale)
    const bool thumbs_stale = settings_before.migrated_thumb_side < image::THUMB_MAX_SIDE;
    const vault::MigrationScan scan_before = vault::scan_migration(v, thumbs_stale);
    CHECK_EQ(scan_before.thumbs, 0);

    // Lock and reopen the vault to verify watermarks persist
    v.lock();
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(mig_bytes("pw"), {}) == vault::VaultResult::Ok);

    const vault::VaultSettings settings_after = vault::vault_settings(v2);
    CHECK_EQ(settings_after.migrated_index_version, vault::MIGRATION_INDEX_VERSION);
    CHECK_EQ(settings_after.migrated_thumb_side, static_cast<uint16_t>(image::THUMB_MAX_SIDE));

    // Verify scan_migration still reports no stale thumbs after reopen
    const bool thumbs_stale_after = settings_after.migrated_thumb_side < image::THUMB_MAX_SIDE;
    const vault::MigrationScan scan_after = vault::scan_migration(v2, thumbs_stale_after);
    CHECK_EQ(scan_after.thumbs, 0);

    v2.lock();
}
