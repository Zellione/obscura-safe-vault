#include "test_framework.h"

#include "ui/import_queue.h"
#include "ui/import_queue_test_helpers.h"
#include "ui/zip_test_helpers.h"
#include "vault/fixtures.h"
#include "vault/staging.h"
#include "vault/transfer.h"
#ifdef OSV_VENDORED_ARCHIVE
#include "ui/archive_test_helpers.h"
#include "ui/volume_import.h"
#endif

#include <fstream>

namespace {
using MakeVault = bool (*)(vault::Vault&, const std::filesystem::path&);
using importtest::drain_until_idle;

bool write_input(const std::filesystem::path& path, std::span<const uint8_t> bytes)
{
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    return static_cast<bool>(out);
}

void check_original(vault::Vault& v, std::string_view path, std::span<const uint8_t> expected,
                    int& _failures)
{
    const auto* node = v.resolve_node(path);
    REQUIRE(node != nullptr);
    crypto::SecureBytes bytes;
    REQUIRE(v.read_image(*node, bytes) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(bytes.as_span(), expected);
}

#ifdef OSV_VENDORED_ARCHIVE
TEST(import_queue_directory_split_7z_selection_survives_reopen)
{
    const auto dir = ziptest::fresh_dir("queue_directory_split_7z");
    const auto original = ziptest::fake_jpeg(63);
    const auto whole = archivetest::make_archive(
        {{"nested/photo.jpg", original}, {"notes.txt", {'n'}}}, "7zip", dir / "photos.7z");
    const auto bytes = archivetest::read_file(whole);
    REQUIRE(bytes.size() > 2);
    const size_t split = bytes.size() / 2;
    REQUIRE(write_input(dir / "photos.7z.001", std::span(bytes).first(split)));
    REQUIRE(write_input(dir / "photos.7z.002", std::span(bytes).subspan(split)));
    const std::vector<std::filesystem::path> picked{
        dir / "photos.7z.002", whole, dir / "photos.7z.001"};
    const auto sources = ui::plan_archive_selection(picked);
    REQUIRE(sources.size() == 2);
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_directory_vault(v, dir / "vault.osv"));
        vault::CommitLane lane;
        lane.start(v);
        v.set_commit_router(&lane);
        ui::ImportQueue queue;
        queue.begin_session(v, lane);
        std::vector<std::filesystem::path> volumes;
        for (const auto& name : sources[0].volumes.volumes) volumes.push_back(dir / name);
        (void)queue.enqueue_volume_set(volumes, sources[0].volumes.style,
                                      sources[0].volumes.stem, "", "Split",
                                      ui::ImportTaskKind::Archive);
        (void)queue.enqueue_archive(sources[1].path, "", "Single", ui::ImportTaskKind::Archive);
        (void)queue.enqueue_archive(sources[1].path, "", "Single", ui::ImportTaskKind::Archive);
        REQUIRE(drain_until_idle(queue));
        const auto rows = queue.snapshot();
        REQUIRE(rows.size() == 3);
        for (const auto& row : std::span(rows).first(2)) {
            CHECK(row.state == ui::ImportTaskState::Done);
            CHECK_EQ(row.imported, 1);
            CHECK_EQ(row.skipped, 1);
        }
        CHECK(rows[2].state == ui::ImportTaskState::Done);
        CHECK_EQ(rows[2].imported, 0);
        CHECK_EQ(rows[2].skipped, 2);
        queue.end_session();
        REQUIRE(lane.flush());
    }
    {
        vault::Vault reopened;
        REQUIRE(vaulttest::open_vault(dir / "vault.osv", reopened));
        check_original(reopened, "Split/nested/photo.jpg", original, _failures);
        check_original(reopened, "Single/nested/photo.jpg", original, _failures);
    }
    ziptest::cleanup_dir(dir);
}
#endif

void collision_and_cancel_survive_reopen(MakeVault create, int& _failures)
{
    const auto dir = ziptest::fresh_dir("osv_queue_backend_collision_cancel");
    const auto path = dir / "vault.osv";
    const auto original = ziptest::fake_jpeg(1);
    const auto incoming = ziptest::fake_jpeg(2);
    REQUIRE(write_input(dir / "existing.jpg", incoming));
    REQUIRE(write_input(dir / "new.jpg", incoming));
    REQUIRE(write_input(dir / "cancelled.jpg", incoming));
    {
        vault::Vault v;
        REQUIRE(create(v, path));
        REQUIRE(v.create_gallery("photos") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("photos", original, "existing.jpg") == vault::VaultResult::Ok);
        vault::CommitLane lane;
        lane.start(v);
        v.set_commit_router(&lane);
        ui::ImportQueue queue;
        queue.begin_session(v, lane);
        queue.set_exclusive(true);
        const auto cancelled = queue.enqueue_files({dir / "cancelled.jpg"}, "photos");
        REQUIRE(queue.cancel(cancelled));
        (void)queue.enqueue_files({dir / "existing.jpg", dir / "new.jpg"}, "photos");
        queue.set_exclusive(false);
        REQUIRE(drain_until_idle(queue));
        const auto snapshot = queue.snapshot();
        REQUIRE(snapshot.size() == 2);
        CHECK(snapshot[0].state == ui::ImportTaskState::Cancelled);
        CHECK_EQ(snapshot[0].imported, 0);
        CHECK(snapshot[1].state == ui::ImportTaskState::Done);
        CHECK_EQ(snapshot[1].imported, 1);
        CHECK_EQ(snapshot[1].skipped, 1);
        queue.end_session();
        REQUIRE(lane.flush());
    }
    {
        vault::Vault reopened;
        REQUIRE(vaulttest::open_vault(path, reopened));
        CHECK_EQ(reopened.list("photos").size(), size_t{2});
        CHECK(reopened.resolve_node("photos/cancelled.jpg") == nullptr);
        check_original(reopened, "photos/existing.jpg", original, _failures);
        check_original(reopened, "photos/new.jpg", incoming, _failures);
    }
    ziptest::cleanup_dir(dir);
}

void nested_archive_review_survives_reopen(MakeVault create, int& _failures)
{
    const auto dir = ziptest::fresh_dir("osv_queue_backend_nested_review");
    const auto path = dir / "vault.osv";
    const auto original = ziptest::fake_jpeg(41);
    const std::string meta = R"json({"tags":[{"name":"reviewed"}]})json";
    const auto nested = ziptest::make_archive(
        {{"photo.jpg", original}, {"meta.json", {meta.begin(), meta.end()}}}, dir / "inner.zip");
    std::vector<uint8_t> nested_bytes;
    {
        std::ifstream in(nested, std::ios::binary);
        nested_bytes.assign(std::istreambuf_iterator<char>(in), {});
    }
    REQUIRE(!nested_bytes.empty());
    const auto archive = ziptest::make_archive({{"inner.zip", nested_bytes}}, dir / "outer.zip");
    {
        vault::Vault v;
        REQUIRE(create(v, path));
        vault::CommitLane lane;
        lane.start(v);
        v.set_commit_router(&lane);
        ui::ImportQueue queue;
        queue.begin_session(v, lane);
        (void)queue.enqueue_archive(archive, "", "Book", ui::ImportTaskKind::Zip);
        REQUIRE(drain_until_idle(queue));
        auto pending = queue.take_tag_review();
        REQUIRE(pending.has_value());
        const auto* gallery = v.resolve_node("Book/inner");
        REQUIRE(gallery != nullptr);
        CHECK(gallery->tags.empty());
        ui::ImportTagReview review(pending->tags(), {});
        REQUIRE(queue.apply_tag_review(*pending, review));
        queue.end_session();
        REQUIRE(lane.flush());
    }
    {
        vault::Vault reopened;
        REQUIRE(vaulttest::open_vault(path, reopened));
        const auto* gallery = reopened.resolve_node("Book/inner");
        REQUIRE(gallery != nullptr);
        REQUIRE(gallery->tags.size() == 1);
        CHECK_EQ(gallery->tags.front().view(), "reviewed");
        CHECK(reopened.resolve_node("Book")->tags.empty());
        check_original(reopened, "Book/inner/photo.jpg", original, _failures);
    }
    ziptest::cleanup_dir(dir);
}
}  // namespace

TEST(import_queue_directory_collision_and_cancel_survive_reopen)
{
    collision_and_cancel_survive_reopen(vaulttest::make_directory_vault, _failures);
}

TEST(import_queue_wait_reports_timeout_with_parked_work)
{
    const auto dir = ziptest::fresh_dir("osv_queue_wait_timeout");
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_directory_vault(v, dir / "vault.osv"));
        vault::CommitLane lane;
        lane.start(v);
        v.set_commit_router(&lane);
        ui::ImportQueue queue;
        queue.begin_session(v, lane);
        queue.set_exclusive(true);
        const auto id = queue.enqueue_files({dir / "never_read.jpg"}, "");
        CHECK(!drain_until_idle(queue, std::chrono::milliseconds(0)));
        REQUIRE(queue.cancel(id));
        CHECK(drain_until_idle(queue));
        queue.end_session();
    }
    ziptest::cleanup_dir(dir);
}

TEST(import_queue_legacy_collision_and_cancel_survive_reopen)
{
    collision_and_cancel_survive_reopen(vaulttest::make_legacy_writable_vault, _failures);
}

TEST(import_queue_directory_nested_review_survives_reopen)
{
    nested_archive_review_survives_reopen(vaulttest::make_directory_vault, _failures);
}

TEST(import_queue_legacy_nested_review_survives_reopen)
{
    nested_archive_review_survives_reopen(vaulttest::make_legacy_writable_vault, _failures);
}

TEST(import_queue_directory_folder_preserves_media_without_unreviewed_tags)
{
    const auto dir = ziptest::fresh_dir("osv_queue_directory_folder_review");
    const auto path = dir / "vault.osv";
    const auto source = dir / "source";
    std::filesystem::create_directories(source / "nested");
    const auto original = ziptest::fake_jpeg(42);
    REQUIRE(write_input(source / "nested/photo.jpg", original));
    std::ofstream(source / "meta.json") << R"json({"tags":[{"name":"unreviewed"}]})json";
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_directory_vault(v, path));
        vault::CommitLane lane;
        lane.start(v);
        v.set_commit_router(&lane);
        ui::ImportQueue queue;
        queue.begin_session(v, lane);
        (void)queue.enqueue_folder(source, "", "Folder");
        REQUIRE(drain_until_idle(queue));
        REQUIRE(queue.take_tag_review().has_value());  // Discarding review is cancellation.
        queue.end_session();
        REQUIRE(lane.flush());
    }
    {
        vault::Vault reopened;
        REQUIRE(vaulttest::open_vault(path, reopened));
        const auto* gallery = reopened.resolve_node("Folder");
        REQUIRE(gallery != nullptr);
        CHECK(gallery->tags.empty());
        check_original(reopened, "Folder/nested/photo.jpg", original, _failures);
    }
    ziptest::cleanup_dir(dir);
}

TEST(import_queue_directory_final_commit_failure_preserves_previous_snapshot)
{
    const auto dir = ziptest::fresh_dir("osv_queue_directory_commit_failure");
    const auto path = dir / "vault.osv";
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_directory_vault(v, path));
        REQUIRE(v.create_gallery("retained") == vault::VaultResult::Ok);
        vault::CommitLane lane;
        lane.start(v);
        v.set_commit_router(&lane);
        ui::ImportQueue queue;
        queue.begin_session(v, lane);
        REQUIRE(vault::ensure_gallery_path(v, "uncommitted") == vault::VaultResult::Ok);
        // Deterministic validation failure before SQL snapshot publication.
        auto* retained = v.resolve_node("retained");
        REQUIRE(retained != nullptr);
        retained->node_id = {};
        queue.abort_and_flush();
        CHECK(queue.lane_failed());
        CHECK(!lane.flush());
        CHECK(!lane.enqueue_snapshot());
        queue.end_session();
    }
    {
        vault::Vault reopened;
        REQUIRE(vaulttest::open_vault(path, reopened));
        CHECK(reopened.resolve_node("retained") != nullptr);
        CHECK(reopened.resolve_node("uncommitted") == nullptr);
    }
    ziptest::cleanup_dir(dir);
}

TEST(directory_transfer_copy_and_move_preserve_bytes_after_reopen)
{
    const auto dir = ziptest::fresh_dir("osv_directory_transfer_reopen");
    const auto original = ziptest::fake_jpeg(43);
    {
        vault::Vault source, destination;
        REQUIRE(vaulttest::make_directory_vault(source, dir / "source.osv"));
        REQUIRE(vaulttest::make_directory_vault(destination, dir / "destination.osv"));
        REQUIRE(source.add_image("", original, "copy.jpg") == vault::VaultResult::Ok);
        REQUIRE(source.add_image("", original, "move.jpg") == vault::VaultResult::Ok);
        REQUIRE(vault::transfer_image(source, "", "copy.jpg", destination, "",
                                      vault::TransferMode::Copy) == vault::VaultResult::Ok);
        REQUIRE(vault::transfer_image(source, "", "move.jpg", destination, "",
                                      vault::TransferMode::Move) == vault::VaultResult::Ok);
    }
    {
        vault::Vault source, destination;
        REQUIRE(vaulttest::open_vault(dir / "source.osv", source));
        REQUIRE(vaulttest::open_vault(dir / "destination.osv", destination));
        CHECK_EQ(source.list("").size(), size_t{1});
        CHECK(source.resolve_node("move.jpg") == nullptr);
        CHECK_EQ(destination.list("").size(), size_t{2});
        check_original(source, "copy.jpg", original, _failures);
        check_original(destination, "copy.jpg", original, _failures);
        check_original(destination, "move.jpg", original, _failures);
    }
    ziptest::cleanup_dir(dir);
}
