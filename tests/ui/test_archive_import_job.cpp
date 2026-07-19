#include "test_framework.h"

#ifdef OSV_VENDORED_ARCHIVE

#include "ui/zip_import_job.h"
#include "archive_test_helpers.h"
#include "zip_test_helpers.h"

#include "vault/vault.h"

#include <chrono>
#include <cstring>
#include <optional>
#include <string_view>
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
        CHECK(job.start_archive(v, archive,
                                {ui::ZipConflictPolicy::AskUser}, "", "Album"));
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

TEST(archive_import_job_runs_encrypted_zip_with_password_to_completion)
{
    auto dir = fresh_dir("archive_job_enc_ok");
    auto data = fake_bytes(31);
    auto archive = archivetest::make_encrypted_zip({{"a.jpg", data}}, "correcthorse",
                                                    dir / "secret.zip");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");

        crypto::SecureBytes pw(std::string_view("correcthorse").size());
        std::memcpy(pw.data(), "correcthorse", pw.size());

        ui::ZipImportJob job;
        CHECK(job.start_archive(v, archive,
                                {ui::ZipConflictPolicy::AskUser}, "", "Secret",
                                /*password_protected=*/true, std::move(pw)));
        auto oc = await_outcome(job);
        REQUIRE(oc.has_value());
        CHECK(oc->ok);
        CHECK_FALSE(oc->needs_password);
        CHECK_EQ(oc->imported, 1);
        CHECK_EQ(v.list("Secret").size(), static_cast<size_t>(1));
    }
    cleanup_dir(dir);
}

TEST(archive_import_job_reports_needs_password_for_wrong_password)
{
    // Retried over fresh fixtures for the same reason as
    // archive_import_encrypted_zip_wrong_password_writes_nothing: the job runs
    // that same verification probe on a worker thread, and traditional ZipCrypto
    // falsely accepts a wrong password about 1 run in 256 (single check byte),
    // which surfaces as a generic failure rather than needs_password. Nothing is
    // ever written either way; only the reported *reason* is probabilistic.
    bool saw_needs_password = false;
    for (int attempt = 0; attempt < 5 && !saw_needs_password; ++attempt) {
        auto dir = fresh_dir("archive_job_enc_wrong");
        auto archive = archivetest::make_encrypted_zip(
            {{"a.jpg", fake_bytes(static_cast<uint8_t>(32 + attempt))}}, "correcthorse",
            dir / "secret.zip");
        {
            vault::Vault v;
            make_vault(v, dir / "v.osv");

            crypto::SecureBytes pw(std::string_view("nope").size());
            std::memcpy(pw.data(), "nope", pw.size());

            ui::ZipImportJob job;
            CHECK(job.start_archive(v, archive,
                                    {ui::ZipConflictPolicy::AskUser}, "", "Secret",
                                    /*password_protected=*/true, std::move(pw)));
            auto oc = await_outcome(job);
            REQUIRE(oc.has_value());
            CHECK(v.list("").empty());
            saw_needs_password = oc->ok && oc->needs_password;
        }
        cleanup_dir(dir);
    }
    CHECK(saw_needs_password);
}

#endif // OSV_VENDORED_ARCHIVE
