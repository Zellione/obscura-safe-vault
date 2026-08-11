#include "test_framework.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "image/anim_info.h"
#include "image/fixtures.h"
#include "image/thumbnail.h"
#include "media/video_probe.h"
#include "ui/migration_job.h"
#include "vault/migration.h"
#include "vault/vault.h"

// NOTE: include image/fixtures.h rather than re-declaring the loaders here.
// load_webp()/load_anim_webp() are `inline` in that header; declaring them
// non-inline compiles but links only by luck at -O0, where other TUs that do
// include the header emit weak out-of-line copies. Release inlines those away
// and the link fails with an undefined reference.

namespace vault {
// Test-only seams defined in tests/vault/test_video.cpp (linked into osv_tests).
// Forward declarations for cross-translation-unit use.
void test_only_force_video_codec_unknown(Vault& v, std::string_view node_path);
void test_only_force_image_animated_unknown(Vault& v, std::string_view node_path);
}

namespace fs = std::filesystem;

static const crypto::KdfParams kJobKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> job_bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

static std::vector<uint8_t> job_pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

// High-entropy bytes from a fixed-seed splitmix64 — deterministic across
// platforms, but incompressible, so a chunk's STORED size tracks its raw size.
// job_pattern() is the opposite: it repeats with period 256 and deflates to
// almost nothing, which makes any "stored bytes >= N" assertion a function of
// the compressor rather than of the payload.
static std::vector<uint8_t> job_incompressible(size_t n, uint64_t seed)
{
    std::vector<uint8_t> v(n);
    uint64_t             x = seed;
    for (size_t i = 0; i < n; ++i) {
        x += 0x9E3779B97F4A7C15ULL;
        uint64_t z = x;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        v[i] = static_cast<uint8_t>(z >> 31);
    }
    return v;
}

#ifdef OSV_VENDORED_AV
// Only the video test below reads a fixture off disk. Guarded to match that
// test's own guard — without FFmpeg this is an unused static and -Werror bites.
static std::vector<uint8_t> read_file(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
#endif

namespace {
struct JobTempVault {
    fs::path path;
    explicit JobTempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_job_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~JobTempVault()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
    [[nodiscard]] std::string str() const { return path.string(); }
};

// Drive the job to completion the way the UI does: poll, then collect once.
ui::MigrationOutcome run_to_completion(ui::MigrationJob& job)
{
    while (job.active()) {
        if (auto out = job.take_outcome()) return *out;
        std::this_thread::yield();
    }
    if (auto out = job.take_outcome()) return *out;
    return {};
}
} // namespace

TEST(migration_job_on_clean_vault_stamps_watermark_and_does_nothing_else)
{
    JobTempVault tv("clean");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", job_pattern(1000, 1), "a.png") == vault::VaultResult::Ok);

    ui::MigrationJob job;
    REQUIRE(job.start(v));
    const ui::MigrationOutcome out = run_to_completion(job);

    CHECK(out.ok);
    CHECK(!out.cancelled);
    CHECK_EQ(out.total, 0);
    CHECK_EQ(out.videos_fixed, 0);
    CHECK_EQ(out.images_fixed, 0);
    CHECK(!vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN, 512));
}

TEST(migration_job_watermark_survives_reopen)
{
    JobTempVault tv("persist");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
                == vault::VaultResult::Ok);
        ui::MigrationJob job;
        REQUIRE(job.start(v));
        const ui::MigrationOutcome out = run_to_completion(job);
        CHECK(out.ok);
    }
    {
        vault::Vault v;
        REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
        REQUIRE(v.unlock(job_bytes("pw"), {}) == vault::VaultResult::Ok);
        CHECK(!vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN, 512));
    }
}

TEST(migration_job_double_start_is_rejected)
{
    JobTempVault tv("double");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);

    ui::MigrationJob job;
    REQUIRE(job.start(v));
    CHECK(!job.start(v));            // already in flight
    (void)run_to_completion(job);
}

TEST(migration_job_fixes_animated_image)
{
    JobTempVault tv("anim_img");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);

    // Add animated WebP
    auto anim_webp = fixtures::load_anim_webp();
    REQUIRE(v.add_image("", anim_webp, "anim.webp") == vault::VaultResult::Ok);

    // Force the animated flag to be unknown (as if migrating from old vault)
    vault::test_only_force_image_animated_unknown(v, "anim.webp");

    // Phase 75: pre-stamp thumbnail watermark so this test focuses on animated detection,
    // not thumbnail regen (which would be an ImageThumb item instead of ImageAnimated).
    auto settings = vault::vault_settings(v);
    settings.migrated_thumb_side = 512;
    REQUIRE(vault::set_vault_settings(v, settings) == vault::VaultResult::Ok);

    // Verify it's marked pending before job runs
    auto scan = vault::scan_migration(v, false);
    CHECK(scan.total() > 0);

    ui::MigrationJob job;
    REQUIRE(job.start(v));
    const ui::MigrationOutcome out = run_to_completion(job);

    // Verify outcome reflects the work
    CHECK(out.ok);
    CHECK(!out.cancelled);
    CHECK_EQ(static_cast<int>(out.total), static_cast<int>(scan.total()));
    CHECK_EQ(out.images_fixed, 1);
    CHECK_EQ(out.videos_fixed, 0);

    // Verify node state changed: animated flag should now be true
    for (const vault::IndexNode* n : v.list("")) {
        if (n->name == "anim.webp") {
            CHECK(n->meta.animated);
            break;
        }
    }

    // Verify watermark was stamped
    CHECK(!vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN, 512));
}

TEST(migration_job_fixes_video_codec)
{
#ifdef OSV_VENDORED_AV
    JobTempVault tv("video_codec");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);

    // Load real MP4 fixture
    auto mp4_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!mp4_bytes.empty());
    REQUIRE(v.add_video("", mp4_bytes, "tiny.mp4", 4096) == vault::VaultResult::Ok);

    // Force codec to Unknown (simulating old vault before this build's FFmpeg)
    vault::test_only_force_video_codec_unknown(v, "tiny.mp4");

    // Verify it's pending
    auto scan = vault::scan_migration(v, false);
    CHECK(scan.total() > 0);

    ui::MigrationJob job;
    REQUIRE(job.start(v));
    const ui::MigrationOutcome out = run_to_completion(job);

    // Verify outcome reflects the attempt
    CHECK(out.ok);
    CHECK(!out.cancelled);
    CHECK_EQ(static_cast<int>(out.total), static_cast<int>(scan.total()));
    CHECK_EQ(out.videos_fixed, 1);
    CHECK_EQ(out.failed, 0);

    // Verify the video node's codec was actually detected
    const std::vector<const vault::IndexNode*> kids = v.list("");
    REQUIRE(kids.size() == 1);
    CHECK(kids[0]->vmeta.codec != vault::VideoCodec::Unknown);
    CHECK(kids[0]->vmeta.width > 0);
    CHECK(kids[0]->vmeta.poster_length > 0);

    // Verify watermark was stamped
    CHECK(!vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN, 512));
#endif
}

TEST(migration_job_take_outcome_returns_exactly_once)
{
    JobTempVault tv("outcome_once");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);

    ui::MigrationJob job;
    REQUIRE(job.start(v));

    while (job.active()) {
        if (auto out = job.take_outcome()) {
            // Got the outcome; verify it's not null
            CHECK(out->ok);
            // Now call again while still active (simulation of UI polling)
            auto out2 = job.take_outcome();
            CHECK(!out2);  // Should be nullopt on second call
            break;
        }
        std::this_thread::yield();
    }

    // After job finishes, take_outcome should return nullopt
    auto final = job.take_outcome();
    CHECK(!final);
}

TEST(migration_job_cancel_prevents_watermark)
{
    JobTempVault tv("cancel_stamp");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);

    // Add animated WebP images so there's work to iterate over
    auto anim_webp = fixtures::load_anim_webp();
    REQUIRE(!anim_webp.empty());
    REQUIRE(v.add_image("", anim_webp, "img.webp") == vault::VaultResult::Ok);

    // Force to unknown animated state so there's pending work
    vault::test_only_force_image_animated_unknown(v, "img.webp");

    ui::MigrationJob job;
    REQUIRE(job.start(v));

    // Immediately cancel (before/during processing)
    job.cancel();

    // Poll for outcome the same way the UI does
    ui::MigrationOutcome out = run_to_completion(job);

    // Verify the outcome reflects the cancel
    CHECK(out.ok);
    CHECK(out.cancelled);

    // Verify watermark was NOT stamped (migration still pending)
    // This is the critical test: that the race condition fix prevents watermark when cancelled
    CHECK(vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN, 512));
}

TEST(migration_job_flips_animated_webp_and_leaves_static_webp_alone)
{
    JobTempVault tv("anim_static_webp");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);

    // Load both animated and static WebP fixtures
    const auto anim_webp = fixtures::load_anim_webp();
    const auto static_webp = fixtures::load_webp();
    REQUIRE(!anim_webp.empty());
    REQUIRE(!static_webp.empty());

    // Add both to the vault
    REQUIRE(v.add_image("", anim_webp, "anim.webp") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", static_webp, "static.webp") == vault::VaultResult::Ok);

    // Force both to the pre-v7 state (animated flag unknown) that the migration repairs
    REQUIRE(vault::apply_image_animated(v, "anim.webp", false) == vault::VaultResult::Ok);
    REQUIRE(vault::apply_image_animated(v, "static.webp", false) == vault::VaultResult::Ok);

    // Phase 75: pre-stamp thumbnail watermark so this test focuses on animated detection
    auto settings = vault::vault_settings(v);
    settings.migrated_thumb_side = 512;
    REQUIRE(vault::set_vault_settings(v, settings) == vault::VaultResult::Ok);

    // Run the migration
    ui::MigrationJob job;
    REQUIRE(job.start(v));
    const ui::MigrationOutcome out = run_to_completion(job);

    CHECK(out.ok);
    CHECK(!out.cancelled);
    CHECK_EQ(out.images_fixed, 2);

    // Verify each image ended up with the correct flag
    for (const vault::IndexNode* n : v.list("")) {
        if (n->name == "anim.webp") {
            CHECK(n->meta.animated);
        } else if (n->name == "static.webp") {
            CHECK(!n->meta.animated);
        }
    }
}

TEST(migration_job_not_reoffered_after_completion)
{
    JobTempVault tv("not_reoffered");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);

    // Add an image that needs repair (animated WebP forced to unknown)
    auto anim_webp = fixtures::load_anim_webp();
    REQUIRE(v.add_image("", anim_webp, "anim.webp") == vault::VaultResult::Ok);
    REQUIRE(vault::apply_image_animated(v, "anim.webp", false) == vault::VaultResult::Ok);

    // Verify migration is pending before the job
    CHECK(vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN, 512));
    CHECK(!vault::scan_migration(v, false).empty());

    // Run the job to completion
    ui::MigrationJob job;
    REQUIRE(job.start(v));
    const ui::MigrationOutcome out = run_to_completion(job);
    CHECK(out.ok);
    CHECK(!out.cancelled);

    // After a successful full pass, migration should NOT be pending
    CHECK(!vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN, 512));
    CHECK(vault::scan_migration(v, false).empty());
}

TEST(migration_job_pool_handles_many_items_without_loss)
{
    JobTempVault tv("pool");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);

    const std::vector<uint8_t> anim = fixtures::load_anim_webp();
    constexpr int kCount = 64;
    for (int i = 0; i < kCount; ++i) {
        const std::string name = "g" + std::to_string(i) + ".webp";
        REQUIRE(v.add_image("", anim, name) == vault::VaultResult::Ok);
        REQUIRE(vault::apply_image_animated(v, name, false) == vault::VaultResult::Ok);
    }

    // Phase 75: pre-stamp thumbnail watermark so this test focuses on animated detection
    auto settings = vault::vault_settings(v);
    settings.migrated_thumb_side = 512;
    REQUIRE(vault::set_vault_settings(v, settings) == vault::VaultResult::Ok);

    ui::MigrationJob job;
    REQUIRE(job.start(v));
    const ui::MigrationOutcome out = run_to_completion(job);

    CHECK(out.ok);
    CHECK_EQ(out.total, kCount);
    CHECK_EQ(out.images_fixed, kCount);   // every item applied, none dropped
    CHECK_EQ(out.failed, 0);
    CHECK_EQ(job.done(), kCount);         // progress reached the denominator

    for (const vault::IndexNode* n : v.list("")) CHECK(n->meta.animated);
}

TEST(migration_job_skips_compaction_when_nothing_is_wasted)
{
    JobTempVault tv("nocompact");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);
    // Empty vault has nothing to compact initially
    REQUIRE(v.list("").size() == 0u);
    REQUIRE(v.wasted_bytes() == 0u);

    ui::MigrationJob job;
    REQUIRE(job.start(v));
    const ui::MigrationOutcome out = run_to_completion(job);

    CHECK(out.ok);
    // Compaction must have been skipped: reclaimed_bytes == 0 is not enough to distinguish
    // skip from compact-failure. Instead, assert residual waste directly:
    // - Waste still in the file proves compact() did not run (successful compact ~= 0 waste)
    // - Waste below floor proves the skip happened for the right reason, not by accident
    // Residual waste is more reliable than file-size checks, which commit itself changes.
    CHECK_EQ(out.reclaimed_bytes, 0u);
    const uint64_t residual = v.wasted_bytes();
    CHECK(residual > 0u);  // waste still exists, so compaction was skipped
    CHECK(residual < vault::Vault::AUTO_COMPACT_MIN_WASTE);  // and below the floor

    // The vault remains valid and readable.
    CHECK_EQ(v.list("").size(), 0u);
}

TEST(migration_job_cancel_skips_compaction)
{
    JobTempVault tv("cancelcompact");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);

    ui::MigrationJob job;
    REQUIRE(job.start(v));
    job.cancel();
    const ui::MigrationOutcome out = run_to_completion(job);

    CHECK_EQ(out.reclaimed_bytes, 0u);
}

TEST(migration_job_compaction_reclaims_orphaned_chunks)
{
    JobTempVault tv("compact_reclaim");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);

    // Add two images: one to keep, one to delete. Deleting the second must leave
    // more than AUTO_COMPACT_MIN_WASTE (256 KiB) orphaned, so the migration job's
    // compaction phase has something to reclaim.
    //
    // Two properties of the payload are load-bearing, and getting either wrong
    // makes this test assert something other than what it means to:
    //
    // 1. INCOMPRESSIBLE. An earlier version pushed 70 MB of job_pattern() through
    //    the framing compressor and relied on the compressed residue landing above
    //    the floor — it cleared it by under 4% (272286 vs 262144), so the REQUIRE
    //    was really asserting a property of deflate.
    //
    // 2. keep >> delete. remove_image() calls auto_reclaim_space(), which reclaims
    //    only when waste >= AUTO_COMPACT_MIN_WASTE AND waste * 4 >= file size. That
    //    reclaim is hole-punching on Linux (logical size unchanged, so wasted_bytes
    //    still reports the holes) but a truncating compact() everywhere else — so on
    //    Windows an auto-reclaimed delete leaves NOTHING for the migration job, and
    //    this test could never pass there. Keeping 24 MiB against a 2 MiB delete puts
    //    waste well under size/4, so the auto gate declines on every platform and the
    //    orphan survives to be reclaimed by the job. MigrationJob's own compaction
    //    gate is the floor alone, with no ratio term, so it still runs.
    const auto keep_bytes   = job_incompressible(24u << 20, 1);
    const auto delete_bytes = job_incompressible(2u << 20, 2);
    REQUIRE(v.add_image("", keep_bytes, "keep.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", delete_bytes, "delete.png") == vault::VaultResult::Ok);
    REQUIRE(v.list("").size() == 2u);

    // Delete the second image, orphaning its ~2 MiB of chunks.
    REQUIRE(v.remove_image("", "delete.png") == vault::VaultResult::Ok);
    REQUIRE(v.list("").size() == 1u);

    // Self-verify the fixture produces enough waste to cross the compaction floor.
    // If this REQUIRE fails, the payload is too small; if the constant changes,
    // this test immediately fails rather than silently testing the skip branch.
    REQUIRE(v.wasted_bytes() >= vault::Vault::AUTO_COMPACT_MIN_WASTE);

    // Record file size before migration to verify compaction actually reclaimed space
    const std::uintmax_t size_before = fs::file_size(tv.path);

    // Run the migration job
    ui::MigrationJob job;
    REQUIRE(job.start(v));
    const ui::MigrationOutcome out = run_to_completion(job);

    // Record file size after migration
    const std::uintmax_t size_after = fs::file_size(tv.path);

    // Verify migration succeeded and compaction ran
    CHECK(out.ok);
    CHECK(!out.cancelled);
    // The reclaimed_bytes is based on wasted() sampled AFTER commit, which may
    // include waste created by the commit itself (superseded index blobs). So
    // reclaimed_bytes >= wasted_before, and must be > 0 since we deleted an image.
    CHECK(out.reclaimed_bytes > 0u);

    // Verify the vault file actually shrank (compaction must have run and freed space)
    CHECK(size_after < size_before);

    // Verify the vault is still readable and contains only the kept image
    REQUIRE(v.list("").size() == 1u);
    CHECK_EQ(v.list("")[0]->name, "keep.png");
}

TEST(migration_job_regenerates_image_thumbs_at_512)
{
    // Phase 75: Build a vault with an image and force migrated_thumb_side to 0
    // to simulate an old vault with stale thumbnails, then run MigrationJob to
    // regenerate at 512px.
    JobTempVault tv("regen_thumbs");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);

    // Add a normal image (which will have a 512px thumb)
    auto webp_data = fixtures::load_webp();
    REQUIRE(v.add_image("", webp_data, "test.webp") == vault::VaultResult::Ok);

    // Verify the image has a thumbnail
    REQUIRE(v.list("").size() == 1u);
    const vault::IndexNode* node = v.list("")[0];
    CHECK(node->meta.thumb_length > 0);

    // Force migrated_thumb_side to 0 to simulate old vault (triggers thumb regen)
    auto settings = vault::vault_settings(v);
    settings.migrated_thumb_side = 0;
    REQUIRE(vault::set_vault_settings(v, settings) == vault::VaultResult::Ok);

    // Verify migration is now pending
    CHECK(vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN, 512));

    // Run migration job
    ui::MigrationJob job;
    REQUIRE(job.start(v));
    const ui::MigrationOutcome out = run_to_completion(job);

    // Verify migration succeeded and thumbs were fixed
    CHECK(out.ok);
    CHECK(!out.cancelled);
    CHECK(out.thumbs_fixed >= 1);

    // Verify watermark was stamped
    CHECK(!vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN, 512));
}

TEST(migration_job_cancel_does_not_stamp_thumb_watermark)
{
    // Phase 75: cancel() before completion -> migrated_thumb_side stays unchanged
    JobTempVault tv("cancel_no_stamp");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);

    // Add an image
    auto webp_data = fixtures::load_webp();
    REQUIRE(v.add_image("", webp_data, "test.webp") == vault::VaultResult::Ok);

    // Force thumb stale to trigger work
    auto settings = vault::vault_settings(v);
    settings.migrated_thumb_side = 0;
    REQUIRE(vault::set_vault_settings(v, settings) == vault::VaultResult::Ok);

    ui::MigrationJob job;
    REQUIRE(job.start(v));

    // Immediately cancel
    job.cancel();

    const ui::MigrationOutcome out = run_to_completion(job);

    // Verify cancel was recorded
    CHECK(out.ok);
    CHECK(out.cancelled);

    // Verify watermark was NOT stamped (migration still pending)
    CHECK(vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN, 512));
}

TEST(migration_job_thumb_arm_skips_when_fresh)
{
    // Phase 75: migrated vault with no pending work -> collect() finds no items
    JobTempVault tv("skip_fresh");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);

    // Add an animated WebP
    auto anim_webp = fixtures::load_anim_webp();
    REQUIRE(v.add_image("", anim_webp, "anim.webp") == vault::VaultResult::Ok);

    // Pre-mark as animated so no animation detection work is needed
    REQUIRE(vault::apply_image_animated(v, "anim.webp", true) == vault::VaultResult::Ok);

    // Manually stamp the vault to mark it as already migrated at 512px
    auto settings = vault::vault_settings(v);
    settings = vault::stamp_migrated(settings, media::PROBE_CAPS_GEN, 512);
    REQUIRE(vault::commit_migration(v, settings) == vault::VaultResult::Ok);

    // Now verify migration is NOT pending and job is a no-op
    CHECK(!vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN, 512));

    ui::MigrationJob job;
    REQUIRE(job.start(v));
    const ui::MigrationOutcome out = run_to_completion(job);

    CHECK(out.ok);
    CHECK(!out.cancelled);
    CHECK_EQ(out.total, 0);  // nothing to do (already migrated, animated known)
    CHECK_EQ(out.thumbs_fixed, 0);
}

