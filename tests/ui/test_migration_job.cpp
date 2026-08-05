#include "test_framework.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "image/anim_info.h"
#include "media/video_probe.h"
#include "ui/migration_job.h"
#include "vault/migration.h"
#include "vault/vault.h"

namespace fixtures {
// Forward declare from test fixtures
std::vector<uint8_t> load_anim_webp();
std::vector<uint8_t> load_webp();
std::vector<uint8_t> solid_png(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b);
}

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

static std::vector<uint8_t> read_file(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

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
    CHECK(!vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN));
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
        CHECK(!vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN));
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

    // Verify it's marked pending before job runs
    auto scan = vault::scan_migration(v);
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
    CHECK(!vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN));
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
    auto scan = vault::scan_migration(v);
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
    CHECK(!vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN));
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
    CHECK(vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN));
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
    CHECK(vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN));
    CHECK(!vault::scan_migration(v).empty());

    // Run the job to completion
    ui::MigrationJob job;
    REQUIRE(job.start(v));
    const ui::MigrationOutcome out = run_to_completion(job);
    CHECK(out.ok);
    CHECK(!out.cancelled);

    // After a successful full pass, migration should NOT be pending
    CHECK(!vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN));
    CHECK(vault::scan_migration(v).empty());
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
    // Empty vault has nothing to compact
    REQUIRE(v.list("").size() == 0u);
    REQUIRE(v.wasted_bytes() == 0u);

    ui::MigrationJob job;
    REQUIRE(job.start(v));
    const ui::MigrationOutcome out = run_to_completion(job);

    CHECK(out.ok);
    // With zero wasted bytes, compaction is skipped and reclaimed_bytes stays 0
    CHECK_EQ(out.reclaimed_bytes, 0u);
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

    // Add two images: one to keep, one to delete
    const auto pattern1 = job_pattern(10000, 1);
    const auto pattern2 = job_pattern(20000, 2);
    REQUIRE(v.add_image("", pattern1, "keep.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern2, "delete.png") == vault::VaultResult::Ok);
    REQUIRE(v.list("").size() == 2u);

    // Delete the second image, which orphans its chunks
    REQUIRE(v.remove_image("", "delete.png") == vault::VaultResult::Ok);
    REQUIRE(v.list("").size() == 1u);

    // Verify there are now wasted bytes from the orphaned chunks
    const uint64_t wasted_before = v.wasted_bytes();
    CHECK(wasted_before > 0u);

    // Run the migration job
    ui::MigrationJob job;
    REQUIRE(job.start(v));
    const ui::MigrationOutcome out = run_to_completion(job);

    // Verify migration succeeded and reclaimed the wasted bytes
    CHECK(out.ok);
    CHECK(!out.cancelled);
    CHECK(out.reclaimed_bytes == wasted_before);
    CHECK(out.reclaimed_bytes > 0u);

    // Verify the vault is still readable and contains only the kept image
    REQUIRE(v.list("").size() == 1u);
    CHECK_EQ(v.list("")[0]->name, "keep.png");
}

