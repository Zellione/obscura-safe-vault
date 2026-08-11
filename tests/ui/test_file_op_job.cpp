#include "test_framework.h"

#include "crypto/kdf.h"
#include "ui/file_op_job.h"
#include "ui/zip_test_helpers.h"   // make_vault, fake_jpeg, fresh_dir, cleanup_dir
#include "vault/transfer.h"
#include "vault/vault.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

using ziptest::cleanup_dir;
using ziptest::fake_jpeg;
using ziptest::fresh_dir;
using ziptest::make_vault;

namespace {
namespace fs = std::filesystem;

// Poll take_outcome() with a generous timeout so the test never hangs CI.
std::optional<ui::FileOpOutcome> await_outcome(ui::FileOpJob& job)
{
    for (int i = 0; i < 5000; ++i) {
        if (auto oc = job.take_outcome()) return oc;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
}

[[nodiscard]] bool seed_images(vault::Vault& v, const char* g, int n)
{
    if (v.create_gallery(g) != vault::VaultResult::Ok) return false;
    for (int i = 1; i <= n; ++i)
        if (v.add_image(g, fake_jpeg(static_cast<uint8_t>(i)),
                        std::to_string(i) + ".jpg") != vault::VaultResult::Ok)
            return false;
    return true;
}

std::vector<const vault::IndexNode*> list_copy(vault::Vault& v, const char* g)
{
    std::vector<const vault::IndexNode*> out;
    for (const auto* n : v.list(g)) out.push_back(n);
    return out;
}
} // namespace

TEST(file_op_job_exports_on_worker_thread)
{
    auto dir = fresh_dir("osv_fj_export");
    auto out = fresh_dir("osv_fj_export_out");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(seed_images(v, "g", 3));

        ui::FileOpJob job;
        CHECK(job.start_export(v, list_copy(v, "g"), out, "out"));
        CHECK(job.active());

        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK(oc->ok);
        CHECK_EQ(oc->done, 3);
        CHECK_EQ(job.total(), 3);
        CHECK_FALSE(job.active());

        // Three files really landed on disk.
        int written = 0;
        for (const auto& e : fs::directory_iterator(out)) { (void)e; ++written; }
        CHECK_EQ(written, 3);
    }
    cleanup_dir(dir);
    cleanup_dir(out);
}

TEST(file_op_job_deletes_gallery_on_worker_thread)
{
    auto dir = fresh_dir("osv_fj_delete");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(seed_images(v, "album", 4));

        ui::FileOpJob job;
        CHECK(job.start_delete(v, "", "album", /*is_gallery=*/true, /*item_total=*/4));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK(oc->ok);
        CHECK_EQ(oc->done, 4);
        CHECK_EQ(v.list("").size(), static_cast<size_t>(0));   // subtree gone
    }
    cleanup_dir(dir);
}

TEST(file_op_job_deletes_single_media)
{
    auto dir = fresh_dir("osv_fj_del1");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(seed_images(v, "g", 2));

        ui::FileOpJob job;
        CHECK(job.start_delete(v, "g", "1.jpg", /*is_gallery=*/false, /*item_total=*/1));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK(oc->ok);
        CHECK_EQ(oc->done, 1);
        CHECK_EQ(v.list("g").size(), static_cast<size_t>(1));   // one image left
    }
    cleanup_dir(dir);
}

TEST(file_op_job_delete_of_missing_item_reports_error)
{
    auto dir = fresh_dir("osv_fj_delmiss");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(seed_images(v, "g", 1));

        ui::FileOpJob job;
        CHECK(job.start_delete(v, "g", "nope.jpg", /*is_gallery=*/false, /*item_total=*/1));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK_FALSE(oc->ok);
        CHECK_FALSE(oc->error.empty());
    }
    cleanup_dir(dir);
}

TEST(file_op_job_copies_images_leaving_source)
{
    auto dir = fresh_dir("osv_fj_copy");
    {
        vault::Vault src, dst;
        make_vault(src, dir / "s.osv");
        make_vault(dst, dir / "d.osv");
        REQUIRE(seed_images(src, "a", 2));
        REQUIRE(dst.create_gallery("b") == vault::VaultResult::Ok);

        ui::FileOpJob job;
        CHECK(job.start_transfer_images(src, "a", {"1.jpg", "2.jpg"}, dst, "b",
                                        vault::TransferMode::Copy, "dst"));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK(oc->ok);
        CHECK_EQ(oc->done, 2);
        CHECK_EQ(dst.list("b").size(), static_cast<size_t>(2));
        CHECK_EQ(src.list("a").size(), static_cast<size_t>(2));   // Copy leaves source
    }
    cleanup_dir(dir);
}

TEST(file_op_job_transfers_images_between_vaults)
{
    auto dir = fresh_dir("osv_fj_xfer");
    {
        vault::Vault src, dst;
        make_vault(src, dir / "s.osv");
        make_vault(dst, dir / "d.osv");
        REQUIRE(seed_images(src, "a", 2));
        REQUIRE(dst.create_gallery("b") == vault::VaultResult::Ok);

        ui::FileOpJob job;
        CHECK(job.start_transfer_images(src, "a", {"1.jpg", "2.jpg"}, dst, "b",
                                        vault::TransferMode::Move, "dst"));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK(oc->ok);
        CHECK_EQ(oc->done, 2);
        CHECK_EQ(dst.list("b").size(), static_cast<size_t>(2));
        CHECK_EQ(src.list("a").size(), static_cast<size_t>(0));
    }
    cleanup_dir(dir);
}

TEST(file_op_job_transfers_gallery_subtree)
{
    auto dir = fresh_dir("osv_fj_gxfer");
    {
        vault::Vault src, dst;
        make_vault(src, dir / "s.osv");
        make_vault(dst, dir / "d.osv");
        REQUIRE(seed_images(src, "album", 3));

        ui::FileOpJob job;
        CHECK(job.start_transfer_gallery(src, "album", dst, "", vault::TransferMode::Copy,
                                         vault::CollisionPolicy::Fail, "dst"));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK(oc->ok);
        CHECK_EQ(oc->done, 3);
        CHECK_EQ(dst.list("album").size(), static_cast<size_t>(3));
        CHECK_EQ(src.list("album").size(), static_cast<size_t>(3));   // Copy leaves source
    }
    cleanup_dir(dir);
}

TEST(file_op_job_runs_one_at_a_time)
{
    auto dir = fresh_dir("osv_fj_one");
    auto out = fresh_dir("osv_fj_one_out");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(seed_images(v, "g", 3));

        ui::FileOpJob job;
        CHECK(job.start_export(v, list_copy(v, "g"), out, "out"));
        // A second start while the first is in flight is refused.
        CHECK_FALSE(job.start_export(v, list_copy(v, "g"), out, "out"));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
    }
    cleanup_dir(dir);
    cleanup_dir(out);
}


// --- Phase 68: one transfer for a mixed selection --------------------------

namespace {
std::vector<uint8_t> read_fixture(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

static std::vector<uint8_t> blob(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 23 + seed);
    return v;
}

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

static const vault::IndexNode* find_child(const vault::Vault& v, std::string_view gallery,
                                          std::string_view name)
{
    for (const auto* c : v.list(gallery)) if (c->name == name) return c;
    return nullptr;
}

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_fj_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec; fs::remove(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove(path, ec); }
    std::string str() const { return path.string(); }
};

static const crypto::KdfParams kKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};
}  // namespace

// Images + a video + a sub-gallery, Space-selected together, move in ONE run:
// media into the target, the gallery subtree under it.
TEST(file_op_job_transfers_mixed_media_and_galleries)
{
    auto dir = fresh_dir("osv_fj_mixed");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(seed_images(v, "a", 2));
        const auto mp4 = read_fixture(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
        REQUIRE(!mp4.empty());
        REQUIRE(v.add_video("a", mp4, "clip.mp4", 4096) == vault::VaultResult::Ok);
        REQUIRE(seed_images(v, "a/sub", 1));
        REQUIRE(v.create_gallery("dst") == vault::VaultResult::Ok);

        ui::FileOpJob job;
        CHECK(job.start_transfer_collection(v, {{{.parent = "a", .names = {"1.jpg", "2.jpg", "clip.mp4"}}},
                                                 {"a/sub"}},
                                            v, "dst", vault::TransferMode::Move,
                                            vault::CollisionPolicy::Fail, "dst"));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK(oc->ok);
        CHECK_EQ(oc->done, 4);                                     // 3 media + sub's 1 file
        CHECK_EQ(v.list("dst").size(), static_cast<size_t>(4));    // sub + 3 media tiles
        CHECK_EQ(v.list("dst/sub").size(), static_cast<size_t>(1));
        CHECK_EQ(v.list("a").size(), static_cast<size_t>(0));      // everything moved out
    }
    cleanup_dir(dir);
}

// A video in a plain multi-selection must transfer — the selection route used
// to filter on is_image() and silently dropped videos.
TEST(file_op_job_transfer_includes_videos)
{
    auto dir = fresh_dir("osv_fj_video_sel");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(v.create_gallery("a") == vault::VaultResult::Ok);
        const auto mp4 = read_fixture(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
        REQUIRE(!mp4.empty());
        REQUIRE(v.add_video("a", mp4, "clip.mp4", 4096) == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("b") == vault::VaultResult::Ok);

        ui::FileOpJob job;
        CHECK(job.start_transfer_collection(v, {{{.parent = "a", .names = {"clip.mp4"}}}, {}},
                                            v, "b", vault::TransferMode::Move,
                                            vault::CollisionPolicy::Fail, "b"));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK(oc->ok);
        CHECK_EQ(oc->done, 1);
        CHECK_EQ(v.list("b").size(), static_cast<size_t>(1));
        CHECK_EQ(v.list("a").size(), static_cast<size_t>(0));
    }
    cleanup_dir(dir);
}

// Phase 68: a favorites/tag/search selection spans parents; the grouped
// transfer moves every group in one job run.
TEST(file_op_job_grouped_transfer_moves_across_parents)
{
    auto dir = fresh_dir("osv_fj_grouped");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(seed_images(v, "a", 1));
        REQUIRE(seed_images(v, "b", 2));
        REQUIRE(v.create_gallery("dst") == vault::VaultResult::Ok);

        std::vector<ui::ParentGroup> groups{
            {.parent = "a", .names = {"1.jpg"}},
            {.parent = "b", .names = {"2.jpg"}},
        };

        ui::FileOpJob job;
        CHECK(job.start_transfer_media_grouped(v, std::move(groups), v, "dst",
                                               vault::TransferMode::Move, "dst"));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK(oc->ok);
        CHECK_EQ(oc->done, 2);
        CHECK_EQ(v.list("dst").size(), static_cast<size_t>(2));
        CHECK_EQ(v.list("a").size(), static_cast<size_t>(0));
        CHECK_EQ(v.list("b").size(), static_cast<size_t>(1));   // b/1.jpg stays
    }
    cleanup_dir(dir);
}

// A destination collision is reported as a skip in the outcome and status
// line, and produces no failure-list entry.
TEST(file_op_job_transfer_reports_skips)
{
    auto dir = fresh_dir("osv_fj_skip");
    {
        vault::Vault src, dst;
        make_vault(src, dir / "s.osv");
        make_vault(dst, dir / "d.osv");
        REQUIRE(seed_images(src, "g", 2));
        REQUIRE(dst.add_image("", fake_jpeg(3), "2.jpg") == vault::VaultResult::Ok);

        ui::FileOpJob job;
        CHECK(job.start_transfer_images(src, "g", {"1.jpg", "2.jpg"}, dst, "",
                                        vault::TransferMode::Move, "dest"));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK(oc->ok);
        CHECK_EQ(oc->done, 1);
        CHECK_EQ(oc->skipped, 1);
        CHECK_EQ(oc->failed, 0);
        CHECK(oc->failures.empty());
        CHECK(oc->status.find("1 skipped") != std::string::npos);
    }
    cleanup_dir(dir);
}

// The Combine policy flows through the job into the vault layer.
TEST(file_op_job_transfer_gallery_combine_policy)
{
    using enum vault::VaultResult;
    TempVault sa("cp_s"), da("cp_d");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("G") == Ok);
    REQUIRE(src.add_image("G", blob(2000, 1), "a.jpg") == Ok);
    REQUIRE(dst.create_gallery("G") == Ok);

    ui::FileOpJob job;
    REQUIRE(job.start_transfer_gallery(src, "G", dst, "", vault::TransferMode::Move,
                                       vault::CollisionPolicy::Combine, "dest"));
    auto oc = await_outcome(job);
    REQUIRE(oc.has_value());
    CHECK(oc->ok);
    CHECK(find_child(dst, "G", "a.jpg") != nullptr);   // merged, not errored
}

// Phase 74: batch delete removes multiple items in one commit
TEST(file_op_job_delete_batch_removes_selection_in_one_run)
{
    TempVault tv("batch");
    vault::Vault v;
    using enum vault::VaultResult;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("g") == Ok);
    REQUIRE(v.add_image("g", blob(300, 1), "a.png") == Ok);
    REQUIRE(v.add_image("", blob(300, 2), "b.png") == Ok);
    REQUIRE(v.add_image("", blob(300, 3), "keep.png") == Ok);

    ui::FileOpJob job;
    // item_total: gallery g (1 image + itself = 2) + b.png = 3
    REQUIRE(job.start_delete_batch(v, {"g", "b.png", "ghost.png"}, 3));
    auto oc = await_outcome(job);
    REQUIRE(oc.has_value());
    CHECK(oc->ok);
    CHECK(oc->kind == ui::FileOpKind::Delete);
    CHECK_EQ(oc->done, 3);
    CHECK(oc->status.find("Removed 2 items") != std::string::npos);
    CHECK(oc->status.find("(1 missing)") != std::string::npos);
    CHECK_EQ(v.list("").size(), size_t{1});
    CHECK_EQ(v.list("")[0]->name, std::string("keep.png"));
}

// Copy-mode combine through the job wrapper: the source gallery keeps all its
// files, the destination gains them, and the status verb says "copied".
TEST(file_op_job_combine_copy_keeps_source)
{
    auto dir = fresh_dir("osv_fj_combine_copy");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(seed_images(v, "src", 2));
        REQUIRE(v.create_gallery("dst") == vault::VaultResult::Ok);

        ui::FileOpJob job;
        CHECK(job.start_combine(v, "src", v, "dst", vault::TransferMode::Copy, "dst"));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK(oc->ok);
        CHECK_EQ(oc->done, 2);
        CHECK_EQ(static_cast<int>(v.list("src").size()), 2);   // copy: source intact
        CHECK_EQ(static_cast<int>(v.list("dst").size()), 2);
        CHECK(oc->status.find("copied") != std::string::npos);
    }
    cleanup_dir(dir);
}

// Move-mode combine keeps its historical wording ("moved") and empties the
// source.
TEST(file_op_job_combine_move_empties_source)
{
    auto dir = fresh_dir("osv_fj_combine_move");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(seed_images(v, "src", 2));
        REQUIRE(v.create_gallery("dst") == vault::VaultResult::Ok);

        ui::FileOpJob job;
        CHECK(job.start_combine(v, "src", v, "dst", vault::TransferMode::Move, "dst"));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK(oc->ok);
        CHECK_EQ(oc->done, 2);
        CHECK(v.list("src").empty());   // move: fully emptied (and removed)
        CHECK_EQ(static_cast<int>(v.list("dst").size()), 2);
        CHECK(oc->status.find("moved") != std::string::npos);
    }
    cleanup_dir(dir);
}
