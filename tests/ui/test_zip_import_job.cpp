#include "test_framework.h"
#include "ui/zip_import_job.h"
#include "ui/zip_test_helpers.h"
#include "vault/vault.h"

#include <chrono>
#include <optional>
#include <thread>

using ziptest::cleanup_dir;
using ziptest::fake_jpeg;
using ziptest::fresh_dir;
using ziptest::make_archive;
using ziptest::make_vault;

// Poll take_outcome() with a generous timeout so the test never hangs CI.
static std::optional<ui::ZipImportOutcome> await_outcome(ui::ZipImportJob& job)
{
    for (int i = 0; i < 5000; ++i) {
        if (auto oc = job.take_outcome()) return oc;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
}

TEST(zip_import_job_runs_cbz_to_completion)
{
    auto dir = fresh_dir("osv_job_done");
    auto cbz = make_archive({{"1.jpg", fake_jpeg(1)}, {"2.jpg", fake_jpeg(2)}}, dir / "J.cbz");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");

        ui::ZipImportJob job;
        CHECK(job.start_cbz(v, cbz, "", "J"));
        CHECK(job.active());   // running on the worker thread

        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK(oc->ok);
        CHECK_EQ(oc->imported, 2);
        CHECK_FALSE(job.active());   // collected -> idle again

        // The pages really landed in the vault (worker mutated it, not the poller).
        CHECK_EQ(v.list("J").size(), static_cast<size_t>(2));
    }
    cleanup_dir(dir);
}

TEST(zip_import_job_reports_final_progress)
{
    auto dir = fresh_dir("osv_job_prog");
    auto cbz = make_archive({{"1.jpg", fake_jpeg(1)},
                             {"2.jpg", fake_jpeg(2)},
                             {"3.jpg", fake_jpeg(3)}},
                            dir / "J.cbz");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");

        ui::ZipImportJob job;
        CHECK(job.start_cbz(v, cbz, "", "J"));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK_EQ(job.total(), 3);
        CHECK_EQ(job.done(), 3);
    }
    cleanup_dir(dir);
}

TEST(zip_import_job_take_outcome_is_null_until_done)
{
    // Before any job is started there is nothing to collect.
    ui::ZipImportJob job;
    CHECK_FALSE(job.active());
    CHECK_FALSE(job.take_outcome().has_value());
}

TEST(zip_import_job_runs_zip_new_gallery_to_completion)
{
    auto img = fake_jpeg(1);
    auto dir = fresh_dir("osv_job_zip");
    auto zip = make_archive({{"2020/a.jpg", img}, {"2020/b.jpg", img}}, dir / "in.zip");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");

        ui::ZipImportJob job;
        CHECK(job.start_zip(v, zip, "", "Album",
                            ui::ZipConflictPolicy::AskUser));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK(oc->ok);
        CHECK_FALSE(oc->needs_resolution);
        CHECK_EQ(oc->imported, 2);
        CHECK_EQ(v.list("Album/2020").size(), static_cast<size_t>(2));
    }
    cleanup_dir(dir);
}

TEST(zip_import_job_surfaces_needs_resolution_without_writing)
{
    // "a" holds an image AND a subfolder with media -> mixed; AskUser must come
    // back needs_resolution with nothing written (the worker only planned).
    auto img = fake_jpeg(3);
    auto dir = fresh_dir("osv_job_zip_mix");
    auto zip = make_archive({{"a/x.jpg", img}, {"a/b/y.jpg", img}}, dir / "in.zip");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");

        ui::ZipImportJob job;
        CHECK(job.start_zip(v, zip, "", "G",
                            ui::ZipConflictPolicy::AskUser));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK(oc->ok);
        CHECK(oc->needs_resolution);
        CHECK_EQ(oc->imported, 0);
        CHECK(v.list("G").empty());
    }
    cleanup_dir(dir);
}
