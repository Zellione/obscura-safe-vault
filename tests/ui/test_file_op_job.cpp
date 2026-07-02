#include "test_framework.h"

#include "ui/file_op_job.h"
#include "ui/zip_test_helpers.h"   // make_vault, fake_jpeg, fresh_dir, cleanup_dir
#include "vault/vault.h"

#include <chrono>
#include <optional>
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
        CHECK(job.start_transfer_gallery(src, "album", dst, "", vault::TransferMode::Copy, "dst"));
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
