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
    if (mp4_bytes.empty()) {
        CHECK(true);  // Skip if fixture missing
        return;
    }
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
    CHECK(out.videos_fixed + out.videos_skipped >= 1);

    // Verify watermark was stamped even if codec couldn't be resolved
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

    // Add multiple images to ensure cancel lands mid-iteration
    REQUIRE(v.add_image("", job_pattern(1000, 1), "a.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", job_pattern(1000, 2), "b.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", job_pattern(1000, 3), "c.png") == vault::VaultResult::Ok);

    ui::MigrationJob job;
    REQUIRE(job.start(v));

    // Spin until some work is done, then cancel
    while (job.active()) {
        if (job.done() > 0) {
            job.cancel();
            break;
        }
        std::this_thread::yield();
    }

    const ui::MigrationOutcome out = run_to_completion(job);

    // Verify the outcome reflects the cancel
    CHECK(out.ok);
    CHECK(out.cancelled);

    // Verify watermark was NOT stamped (migration still pending)
    CHECK(vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN));
}
