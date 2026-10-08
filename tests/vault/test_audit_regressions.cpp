// Regression coverage for the phase 110–115 audit. Synthetic data only.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "image/fixtures.h"
#include "test_framework.h"
#include "ui/dup_scan.h"
#include "ui/migration_job.h"
#include "ui/tile_thumb.h"
#include "vault/commit_lane.h"
#include "vault/legacy_converter.h"
#include "vault/staging.h"
#include "vault/v3_read_session.h"
#include "vault/vault.h"

namespace {
void require_fixture(bool ok)
{
    if (!ok) throw std::runtime_error("audit fixture setup failed");
}
constexpr std::array<uint8_t, 2> password{'p', 'w'};
constexpr crypto::KdfParams kdf{1, 8, 1};
struct Fixture {
    std::filesystem::path path;
    vault::Vault source;
    Fixture()
    {
        char pattern[] = "/tmp/osv-audit-XXXXXX";
        path = ::mkdtemp(pattern);
        require_fixture(vault::Vault::create((path / "source.osv").string(), password, {}, kdf,
                                             source) == vault::VaultResult::Ok);
    }
    ~Fixture()
    {
        source.lock();
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    void image(const char* name)
    {
        constexpr std::array<uint8_t, 4> bytes{1, 2, 3, 4};
        require_fixture(source.add_image("", bytes, name) == vault::VaultResult::Ok);
    }
    vault::LegacyConversionReport convert()
    {
        source.lock();
        require_fixture(source.unlock(password, {}) == vault::VaultResult::Ok);
        return vault::convert_legacy_vault(source, {path / "dest.osv", password, {}, kdf});
    }
};
}  // namespace
TEST(audit_tag_order_conversion)
{
    Fixture f;
    f.image("one");
    REQUIRE(f.source.set_tags("one", {"zebra", "alpha"}) == vault::VaultResult::Ok);
    auto r = f.convert();
    std::printf("AUDIT tag_order status=%d deep=%d cold=%d logical=%d\n", int(r.status),
                r.deep_verified, r.cold_reopened, r.logical_verified);
    CHECK(r.status == vault::LegacyConversionStatus::Ok);
}
TEST(audit_tag_case_conversion)
{
    Fixture f;
    f.image("one");
    f.image("two");
    REQUIRE(f.source.set_tags("one", {"Blue"}) == vault::VaultResult::Ok);
    REQUIRE(f.source.set_tags("two", {"blue"}) == vault::VaultResult::Ok);
    auto r = f.convert();
    std::printf("AUDIT tag_case status=%d\n", int(r.status));
    CHECK(r.status == vault::LegacyConversionStatus::Ok);
}
TEST(audit_video_duration_conversion)
{
    Fixture f;
    constexpr std::array<uint8_t, 4> bytes{1, 2, 3, 4};
    vault::StagedVideoInfo meta;
    meta.container = vault::VideoContainer::MP4;
    meta.duration_us = 1'234'567;
    REQUIRE(vault::add_video_prestaged(f.source, "", bytes, "video.mp4", meta, 0) ==
            vault::VaultResult::Ok);
    auto r = f.convert();
    std::printf("AUDIT duration status=%d\n", int(r.status));
    CHECK(r.status == vault::LegacyConversionStatus::Ok);
}
TEST(audit_partial_conversion_opens_normally)
{
    Fixture f;
    f.image("one");
    std::atomic_bool cancel{true};
    auto r =
        vault::convert_legacy_vault(f.source, {f.path / "dest.osv", password, {}, kdf, &cancel});
    REQUIRE(r.status == vault::LegacyConversionStatus::Cancelled);
    vault::Vault partial;
    REQUIRE(vault::Vault::open((f.path / "dest.osv").string(), partial) == vault::VaultResult::Ok);
    auto opened = partial.unlock(password, {});
    std::printf("AUDIT partial unlock=%d readonly=%d\n", int(opened),
                vault::vault_is_read_only(partial));
    CHECK(opened != vault::VaultResult::Ok);
}
TEST(audit_stale_header_after_password_change)
{
    Fixture f;
    vault::Vault first, stale;
    auto path = (f.path / "new.osv").string();
    REQUIRE(vault::Vault::create_directory(path, password, {}, kdf, first) ==
            vault::VaultResult::Ok);
    REQUIRE(vault::Vault::open(path, stale) == vault::VaultResult::Ok);
    constexpr std::array<uint8_t, 3> replacement{'n', 'e', 'w'};
    REQUIRE(first.change_password(password, {}, replacement, {}) == vault::VaultResult::Ok);
    first.lock();
    auto result = stale.unlock(password, {});
    std::printf("AUDIT stale_header old_password_unlock=%d\n", int(result));
    CHECK(result == vault::VaultResult::AuthFailed);
}
TEST(audit_import_cache_identity)
{
    Fixture f;
    vault::Vault v;
    REQUIRE(vault::Vault::create_directory((f.path / "new.osv").string(), password, {}, kdf, v) ==
            vault::VaultResult::Ok);
    constexpr std::array<uint8_t, 4> first{1, 2, 3, 4}, second{5, 6, 7, 8};
    vault::StagedThumb thumb;
    REQUIRE(thumb.thumb_jpeg.assign(first));
    REQUIRE(vault::add_image_prestaged(v, "", first, "one", thumb, 0) == vault::VaultResult::Ok);
    REQUIRE(vault::add_image_prestaged(v, "", second, "two", thumb, 0) == vault::VaultResult::Ok);
    auto one = ui::thumb_key_for(*v.resolve_node("one"));
    auto two = ui::thumb_key_for(*v.resolve_node("two"));
    std::printf("AUDIT imported keys=%llu,%llu\n", (unsigned long long)one.key,
                (unsigned long long)two.key);
    CHECK(one.key != two.key);
}
TEST(audit_original_span_reads_thumbnail)
{
    Fixture f;
    vault::Vault v;
    REQUIRE(vault::Vault::create_directory((f.path / "new.osv").string(), password, {}, kdf, v) ==
            vault::VaultResult::Ok);
    constexpr std::array<uint8_t, 4> original{1, 2, 3, 4}, thumbbytes{5, 6, 7, 8};
    vault::StagedThumb thumb;
    REQUIRE(thumb.thumb_jpeg.assign(thumbbytes));
    REQUIRE(vault::add_image_prestaged(v, "", original, "one", thumb, 0) == vault::VaultResult::Ok);
    crypto::SecureBytes result;
    REQUIRE(vault::read_thumb_span(v, vault::image_data_chunk_ref(*v.resolve_node("one")),
                                   result) == vault::VaultResult::Ok);
    std::printf("AUDIT original span first byte=%d expected=1\n", int(result.data()[0]));
    CHECK(std::ranges::equal(result.as_span(), original));
}
TEST(audit_false_exact_duplicates)
{
    Fixture f;
    vault::Vault v;
    REQUIRE(vault::Vault::create_directory((f.path / "new.osv").string(), password, {}, kdf, v) ==
            vault::VaultResult::Ok);
    constexpr std::array<uint8_t, 4> first{1, 2, 3, 4}, second{5, 6, 7, 8};
    vault::StagedThumb thumb;
    REQUIRE(thumb.thumb_jpeg.assign(first));
    REQUIRE(vault::add_image_prestaged(v, "", first, "one", thumb, 0) == vault::VaultResult::Ok);
    REQUIRE(vault::add_image_prestaged(v, "", second, "two", thumb, 0) == vault::VaultResult::Ok);
    ui::DupScanJob job;
    job.start(v, ui::collect_scan_items(v), false);
    while (job.active())
        std::this_thread::yield();
    auto result = job.take_outcome();
    REQUIRE(result.has_value());
    std::printf("AUDIT false exact groups=%zu skipped=%zu\n", result->groups.size(),
                result->skipped);
    CHECK(result->groups.empty());
}
TEST(audit_directory_import_commit_lane)
{
    Fixture f;
    vault::Vault v;
    REQUIRE(vault::Vault::create_directory((f.path / "new.osv").string(), password, {}, kdf, v) ==
            vault::VaultResult::Ok);
    vault::CommitLane lane;
    lane.start(v);
    REQUIRE(lane.enqueue_snapshot());
    REQUIRE(lane.flush());
    lane.stop();
}
TEST(audit_case_distinct_siblings)
{
    Fixture f;
    REQUIRE(f.source.create_gallery("Trip") == vault::VaultResult::Ok);
    REQUIRE(f.source.create_gallery("trip") == vault::VaultResult::Ok);
    auto r = f.convert();
    std::printf("AUDIT case siblings conversion=%d\n", int(r.status));
    CHECK(r.status == vault::LegacyConversionStatus::Ok);
}
TEST(audit_verify_media_without_original)
{
    Fixture f;
    auto created = vault::v3::ReadSession::create(f.path / "new.osv", password, {}, kdf);
    REQUIRE(created.session);
    auto root = created.session->root();
    auto image = vault::IndexNode::image("missing.jpg");
    image.node_id = root.node_id;
    image.node_id[0] ^= 0x80;
    image.meta.format = vault::ImageFormat::JPEG;
    image.meta.orig_size = 4;
    root.children.push_back(std::move(image));
    REQUIRE(created.session->commit_metadata(root, created.session->settings(), {}) ==
            vault::v3::ReadStatus::Ok);
    auto report = created.session->verify(vault::v3::VerifyDepth::Deep);
    std::printf("AUDIT missing original verify=%d checked=%llu findings=%zu\n", int(report.status),
                (unsigned long long)report.objects_checked, report.findings.size());
    CHECK(report.status != vault::v3::RecoveryStatus::Ok);
}

TEST(audit_missing_object_is_reported_on_normal_unlock)
{
    Fixture f;
    vault::Vault v;
    const auto path = f.path / "new.osv";
    REQUIRE(vault::Vault::create_directory(path.string(), password, {}, kdf, v) ==
            vault::VaultResult::Ok);
    const std::array<uint8_t, 4> bytes{1, 2, 3, 4};
    REQUIRE(v.add_image("", bytes, "one") == vault::VaultResult::Ok);
    v.lock();
    for (const auto& entry : std::filesystem::recursive_directory_iterator(path / "objects"))
        if (entry.is_regular_file()) std::filesystem::remove(entry.path());
    CHECK(v.unlock(password, {}) == vault::VaultResult::Damaged);
    auto diagnostic = vault::v3::ReadSession::open(path);
    REQUIRE(diagnostic.session);
    REQUIRE(diagnostic.session->unlock(password, {}) == vault::v3::ReadStatus::Ok);
    CHECK(diagnostic.session->quick_open_report().has_corruption());
}

TEST(audit_video_original_frames_are_distinct_and_complete)
{
    Fixture f;
    vault::Vault v;
    REQUIRE(vault::Vault::create_directory((f.path / "new.osv").string(), password, {}, kdf, v) ==
            vault::VaultResult::Ok);
    std::vector<uint8_t> bytes(vault::VIDEO_CHUNK_SIZE + 17, 42);
    std::fill(bytes.begin() + vault::VIDEO_CHUNK_SIZE, bytes.end(), 73);
    vault::StagedVideoInfo meta;
    meta.container = vault::VideoContainer::MP4;
    REQUIRE(vault::add_video_prestaged(v, "", bytes, "movie", meta, 0) == vault::VaultResult::Ok);
    REQUIRE(vault::commit_staged(v) == vault::VaultResult::Ok);
    const auto check_frames = [&] {
        const auto* node = v.resolve_node("movie");
        require_fixture(node && node->vmeta.chunks.size() == 2);
        crypto::SecureBytes frame;
        require_fixture(vault::read_thumb_span(v, vault::video_chunk_ref(*node, 0), frame) ==
                        vault::VaultResult::Ok);
        require_fixture(frame.size() == vault::VIDEO_CHUNK_SIZE && frame.data()[0] == 42);
        require_fixture(vault::read_thumb_span(v, vault::video_chunk_ref(*node, 1), frame) ==
                        vault::VaultResult::Ok);
        require_fixture(frame.size() == 17 && frame.data()[0] == 73);
    };
    check_frames();
    v.lock();
    REQUIRE(v.unlock(password, {}) == vault::VaultResult::Ok);
    check_frames();
    ui::DupScanJob job;
    job.start(v, ui::collect_scan_items(v), false);
    while (job.active())
        std::this_thread::yield();
    auto result = job.take_outcome();
    REQUIRE(result);
    CHECK(result->skipped == 0);
}

TEST(audit_staged_objects_survive_an_earlier_queue_snapshot)
{
    Fixture f;
    vault::Vault v;
    REQUIRE(vault::Vault::create_directory((f.path / "new.osv").string(), password, {}, kdf, v) ==
            vault::VaultResult::Ok);
    const std::array<uint8_t, 4> bytes{1, 2, 3, 4};
    auto staged = vault::stage_image(v, bytes, "later");
    REQUIRE(staged.status == vault::VaultResult::Ok);
    REQUIRE(vault::commit_staged(v) == vault::VaultResult::Ok);
    REQUIRE(vault::attach_staged(v, "", std::move(staged.node)) == vault::VaultResult::Ok);
    REQUIRE(vault::commit_staged(v) == vault::VaultResult::Ok);
    v.lock();
    REQUIRE(v.unlock(password, {}) == vault::VaultResult::Ok);
    const auto* node = v.resolve_node("later");
    REQUIRE(node);
    crypto::SecureBytes read;
    REQUIRE(v.read_image(*node, read) == vault::VaultResult::Ok);
    CHECK(std::ranges::equal(read.as_span(), bytes));
}

TEST(audit_replacement_cache_identity_and_uncommitted_reads)
{
    Fixture f;
    vault::Vault v;
    REQUIRE(vault::Vault::create_directory((f.path / "new.osv").string(), password, {}, kdf, v) ==
            vault::VaultResult::Ok);
    const std::array<uint8_t, 4> bytes{1, 2, 3, 4}, replacement{5, 6, 7, 8};
    auto staged = vault::stage_image(v, bytes, "one");
    REQUIRE(staged.status == vault::VaultResult::Ok);
    REQUIRE(vault::attach_staged(v, "", std::move(staged.node)) == vault::VaultResult::Ok);
    const auto* node = v.resolve_node("one");
    REQUIRE(node);
    crypto::SecureBytes read;
    REQUIRE(v.read_image(*node, read) == vault::VaultResult::Ok);
    CHECK(std::ranges::equal(read.as_span(), bytes));
    REQUIRE(vault::apply_image_thumb(v, "one", bytes) == vault::VaultResult::Ok);
    const auto first = ui::thumb_key_for(*node);
    REQUIRE(vault::commit_staged(v) == vault::VaultResult::Ok);
    REQUIRE(vault::apply_image_thumb(v, "one", replacement) == vault::VaultResult::Ok);
    const auto second = ui::thumb_key_for(*node);
    CHECK(first.key != second.key);
    REQUIRE(v.read_thumbnail(*node, read) == vault::VaultResult::Ok);
    CHECK(std::ranges::equal(read.as_span(), replacement));
    REQUIRE(vault::commit_staged(v) == vault::VaultResult::Ok);
    v.lock();
    REQUIRE(v.unlock(password, {}) == vault::VaultResult::Ok);
    CHECK(ui::thumb_key_for(*v.resolve_node("one")).key == second.key);
}

TEST(audit_directory_migration_job_reads_original_with_or_without_thumbnail)
{
    for (const bool has_thumb : {false, true}) {
        Fixture f;
        vault::Vault v;
        REQUIRE(vault::Vault::create_directory((f.path / "new.osv").string(), password, {}, kdf,
                                               v) == vault::VaultResult::Ok);
        const auto bytes = fixtures::load_webp();
        REQUIRE(!bytes.empty());
        vault::StagedThumb metadata;
        metadata.format = vault::ImageFormat::WebP;
        if (has_thumb) REQUIRE(metadata.thumb_jpeg.assign(std::array<uint8_t, 4>{9, 8, 7, 6}));
        REQUIRE(vault::add_image_prestaged(v, "", bytes, "one.webp", metadata, 0) ==
                vault::VaultResult::Ok);
        auto settings = vault::vault_settings(v);
        settings.migrated_thumb_side = 0;
        REQUIRE(vault::commit_migration(v, settings) == vault::VaultResult::Ok);
        ui::MigrationJob job;
        REQUIRE(job.start(v));
        std::optional<ui::MigrationOutcome> result;
        while (!(result = job.take_outcome()))
            std::this_thread::yield();
        CHECK(result->ok);
        CHECK(result->failed == 0);
        CHECK(result->thumbs_fixed == (has_thumb ? 1 : 0));
        const auto* node = v.resolve_node("one.webp");
        REQUIRE(node);
        crypto::SecureBytes read;
        REQUIRE(v.read_image(*node, read) == vault::VaultResult::Ok);
        CHECK(std::ranges::equal(read.as_span(), bytes));
        CHECK((node->meta.thumb_length != 0) == has_thumb);
    }
}

TEST(audit_conversion_preserves_large_tag_domain_and_settings_order)
{
    Fixture f;
    f.image("one");
    f.image("two");
    auto* one = f.source.resolve_node("one");
    auto* two = f.source.resolve_node("two");
    REQUIRE(one && two);
    for (size_t i = 0; i < 4100; ++i)
        (i % 2 ? one : two)->tags.emplace_back("tag-" + std::to_string(i));
    one->tags.emplace_back(std::string(1024, 'x'));
    auto settings = vault::vault_settings(f.source);
    settings.tag_descriptions.emplace_back(crypto::SecureString{"tag-99"},
                                           crypto::SecureString{"first"});
    settings.tag_descriptions.emplace_back(crypto::SecureString{"TAG-2"},
                                           crypto::SecureString{"second"});
    REQUIRE(vault::set_vault_settings(f.source, settings) == vault::VaultResult::Ok);
    const auto result = f.convert();
    CHECK(result.status == vault::LegacyConversionStatus::Ok);
    CHECK(result.logical_verified);
}

namespace vault {
void test_only_conversion_checkpoint(std::function<void(bool, uint64_t)> callback);
namespace v3 {
void test_only_verification_frame_checkpoint(std::function<void(uint32_t)> callback);
}
}  // namespace vault

TEST(audit_conversion_cancel_during_video_copy_and_final_verification)
{
    for (const bool progress_cancel : {false, true}) {
        for (const bool final_verification : {false, true}) {
            Fixture f;
            std::vector<uint8_t> bytes(3 * vault::VIDEO_CHUNK_SIZE, 23);
            vault::StagedVideoInfo meta;
            meta.container = vault::VideoContainer::MP4;
            REQUIRE(vault::add_video_prestaged(f.source, "", bytes, "movie", meta, 0) ==
                    vault::VaultResult::Ok);
            std::atomic_bool direct_cancel{false};
            vault::OpProgress progress;
            auto& cancel = progress_cancel ? progress.cancel : direct_cancel;
            bool reached = false;
            vault::test_only_conversion_checkpoint([&](bool verifying, uint64_t offset) {
                if (final_verification && verifying) {
                    vault::v3::test_only_verification_frame_checkpoint([&](uint32_t frame) {
                        if (frame == 1) {
                            reached = true;
                            cancel = true;
                        }
                    });
                } else if (!final_verification && !verifying && offset > 0) {
                    reached = true;
                    cancel = true;
                }
            });
            const vault::LegacyConversionRequest request{f.path / "dest.osv", password, {}, kdf,
                                                         &direct_cancel,      &progress};
            const auto result = vault::convert_legacy_vault(f.source, request);
            vault::test_only_conversion_checkpoint({});
            vault::v3::test_only_verification_frame_checkpoint({});
            CHECK(reached);
            CHECK(result.status == vault::LegacyConversionStatus::Cancelled);
            CHECK_FALSE(result.logical_verified);
            cancel = false;
            const auto resumed = vault::convert_legacy_vault(f.source, request);
            CHECK(resumed.status == vault::LegacyConversionStatus::Ok);
            CHECK(resumed.logical_verified);
        }
    }
}
