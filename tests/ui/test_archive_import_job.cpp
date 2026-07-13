#include "test_framework.h"

#ifdef OSV_VENDORED_ARCHIVE

#include "ui/zip_import_job.h"
#include "archive_test_helpers.h"
#include "zip_test_helpers.h"

#include "vault/vault.h"

#include <chrono>
#include <optional>
#include <thread>

using archivetest::fake_bytes;
using archivetest::make_archive;
using ziptest::cleanup_dir;
using ziptest::fresh_dir;
using ziptest::make_vault;

// Poll take_outcome() with a generous timeout so the test never hangs CI
// (mirrors test_zip_import_job.cpp's await_outcome).
static std::optional<ui::ZipImportOutcome> await_outcome(ui::ZipImportJob& job)
{
    for (int i = 0; i < 5000; ++i) {
        if (auto oc = job.take_outcome()) return oc;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
}

TEST(archive_import_job_runs_cbz_style_tar_to_completion)
{
    auto dir = fresh_dir("archive_job_cbz");
    auto tar = make_archive({{"1.jpg", fake_bytes(1)}, {"2.jpg", fake_bytes(2)}},
                            "ustar", dir / "J.cbt");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");

        ui::ZipImportJob job;
        CHECK(job.start_archive_cbz(v, tar, "", "J"));
        CHECK(job.active());

        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK(oc->ok);
        CHECK_EQ(oc->imported, 2);
        CHECK_FALSE(job.active());
        CHECK_EQ(v.list("J").size(), static_cast<size_t>(2));
    }
    cleanup_dir(dir);
}

TEST(archive_import_job_runs_7z_new_gallery_to_completion)
{
    auto dir = fresh_dir("archive_job_7z");
    auto data = fake_bytes(1);
    auto archive = make_archive({{"2020/a.jpg", data}, {"2020/b.jpg", data}},
                                "7zip", dir / "in.7z");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");

        ui::ZipImportJob job;
        CHECK(job.start_archive(v, archive, ui::ZipDest::NewGallery, "", "Album",
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

TEST(archive_import_job_reports_final_progress)
{
    auto dir = fresh_dir("archive_job_prog");
    auto tar = make_archive({{"1.jpg", fake_bytes(1)}, {"2.jpg", fake_bytes(2)},
                             {"3.jpg", fake_bytes(3)}},
                            "ustar", dir / "J.cbt");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");

        ui::ZipImportJob job;
        CHECK(job.start_archive_cbz(v, tar, "", "J"));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK_EQ(job.total(), 3);
        CHECK_EQ(job.done(), 3);
    }
    cleanup_dir(dir);
}

#endif // OSV_VENDORED_ARCHIVE
