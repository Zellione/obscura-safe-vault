#include "test_framework.h"

#ifdef OSV_VENDORED_ARCHIVE

#include "ui/archive_import.h"
#include "archive_test_helpers.h"
#include "zip_test_helpers.h"

#include "vault/vault.h"

#include <fstream>

using archivetest::fake_bytes;
using archivetest::fresh_path;
using archivetest::make_archive;
using ziptest::cleanup_dir;
using ziptest::make_vault;

namespace {

std::filesystem::path fresh_dir_local(const char* name)
{
    return ziptest::fresh_dir(name);
}

} // namespace

TEST(archive_import_tar_new_gallery_mirrors_tree)
{
    auto dir = fresh_dir_local("archive_import_new_gallery");
    vault::Vault v;
    make_vault(v, dir / "v.osv");

    auto archive = make_archive({{"ch1/01.jpg", fake_bytes(1)}, {"ch1/02.jpg", fake_bytes(2)},
                                 {"ch2/01.jpg", fake_bytes(3)}},
                                "ustar", dir / "book.tar");

    auto out = ui::import_archive(v, archive,
                                  {"", "Book"});
    REQUIRE(out.ok);
    CHECK_EQ(out.imported, 3);

    auto ch1 = v.list("Book/ch1");
    CHECK_EQ(ch1.size(), size_t{2});
    auto ch2 = v.list("Book/ch2");
    CHECK_EQ(ch2.size(), size_t{1});

    v.lock();
    cleanup_dir(dir);
}

TEST(archive_import_7z_round_trip_checksums)
{
    auto dir = fresh_dir_local("archive_import_7z_checksum");
    vault::Vault v;
    make_vault(v, dir / "v.osv");

    const auto data = fake_bytes(9, 400);
    auto archive = make_archive({{"only.jpg", data}}, "7zip", dir / "one.7z");

    auto out = ui::import_archive(v, archive,
                                  {"", "Gal"});
    REQUIRE(out.ok);
    REQUIRE(out.imported == 1);

    auto children = v.list("Gal");
    REQUIRE(children.size() == size_t{1});
    crypto::SecureBytes read;
    REQUIRE(v.read_image(*children[0], read) == vault::VaultResult::Ok);
    REQUIRE(read.size() == data.size());
    CHECK(std::memcmp(read.data(), data.data(), data.size()) == 0);

    v.lock();
    cleanup_dir(dir);
}

TEST(archive_import_targz_round_trip_checksums)
{
    auto dir = fresh_dir_local("archive_import_targz_checksum");
    vault::Vault v;
    make_vault(v, dir / "v.osv");

    const auto data1 = fake_bytes(3, 250);
    const auto data2 = fake_bytes(4, 250);
    auto archive = make_archive({{"vol/01.jpg", data1}, {"vol/02.jpg", data2}},
                                "gnutar_gz", dir / "book.tar.gz");

    auto out = ui::import_archive(v, archive,
                                  {"", "Vol"});
    REQUIRE(out.ok);
    REQUIRE(out.imported == 2);

    auto children = v.list("Vol/vol");
    REQUIRE(children.size() == size_t{2});
    crypto::SecureBytes read;
    REQUIRE(v.read_image(*children[0], read) == vault::VaultResult::Ok);
    REQUIRE(read.size() == data1.size());
    CHECK(std::memcmp(read.data(), data1.data(), data1.size()) == 0);
    REQUIRE(v.read_image(*children[1], read) == vault::VaultResult::Ok);
    REQUIRE(read.size() == data2.size());
    CHECK(std::memcmp(read.data(), data2.data(), data2.size()) == 0);

    v.lock();
    cleanup_dir(dir);
}

TEST(archive_import_cb7_style_7z_imports_as_one_gallery)
{
    auto dir = fresh_dir_local("archive_import_cb7");
    vault::Vault v;
    make_vault(v, dir / "v.osv");

    auto archive = make_archive({{"2.jpg", fake_bytes(2)}, {"10.jpg", fake_bytes(10)},
                                 {"1.jpg", fake_bytes(1)}},
                                "7zip", dir / "comic.7z");

    auto out = ui::import_archive_cbz(v, archive, "", "Comic7z");
    REQUIRE(out.ok);
    CHECK_EQ(out.imported, 3);

    auto children = v.list("Comic7z");
    REQUIRE(children.size() == size_t{3});
    CHECK_EQ(children[0]->name, "1.jpg");
    CHECK_EQ(children[1]->name, "2.jpg");
    CHECK_EQ(children[2]->name, "10.jpg");

    v.lock();
    cleanup_dir(dir);
}

TEST(archive_import_rejects_malformed_archive)
{
    auto dir = fresh_dir_local("archive_import_malformed");
    vault::Vault v;
    make_vault(v, dir / "v.osv");

    auto bad = dir / "bad.tar";
    std::ofstream(bad, std::ios::binary) << "not a tar file at all, just junk bytes";

    auto out = ui::import_archive(v, bad,
                                  {"", "Gal"});
    CHECK_FALSE(out.ok);
    CHECK_FALSE(out.error.empty());

    v.lock();
    cleanup_dir(dir);
}

TEST(archive_import_writes_no_extra_files)
{
    auto dir = fresh_dir_local("archive_import_no_fs_write");
    vault::Vault v;
    make_vault(v, dir / "v.osv");

    auto archive = make_archive({{"a.jpg", fake_bytes(1)}}, "ustar", dir / "a.tar");

    size_t before = 0;
    for (auto& _ : std::filesystem::directory_iterator(dir)) (void)_, ++before;

    auto out = ui::import_archive(v, archive,
                                  {"", "Gal"});
    REQUIRE(out.ok);

    size_t after = 0;
    for (auto& _ : std::filesystem::directory_iterator(dir)) (void)_, ++after;
    CHECK_EQ(before, after);  // only v.osv + a.tar existed before and after — no temp files

    v.lock();
    cleanup_dir(dir);
}

TEST(archive_import_cbr_style_tar_imports_as_one_gallery_natural_order)
{
    auto dir = fresh_dir_local("archive_import_cbr_natural");
    vault::Vault v;
    make_vault(v, dir / "v.osv");

    auto archive = make_archive({{"2.jpg", fake_bytes(2)}, {"10.jpg", fake_bytes(10)},
                                 {"1.jpg", fake_bytes(1)}},
                                "ustar", dir / "comic.tar");

    auto out = ui::import_archive_cbz(v, archive, "", "Comic");
    REQUIRE(out.ok);
    CHECK_EQ(out.imported, 3);

    auto children = v.list("Comic");
    REQUIRE(children.size() == size_t{3});
    CHECK_EQ(children[0]->name, "1.jpg");
    CHECK_EQ(children[1]->name, "2.jpg");
    CHECK_EQ(children[2]->name, "10.jpg");

    v.lock();
    cleanup_dir(dir);
}

TEST(archive_import_encrypted_zip_correct_password_imports)
{
    auto dir = fresh_dir_local("archive_import_enc_ok");
    vault::Vault v;
    make_vault(v, dir / "v.osv");

    const auto data = fake_bytes(21, 300);
    auto archive = archivetest::make_encrypted_zip({{"one.jpg", data}}, "swordfish",
                                                    dir / "secret.zip");

    auto out = ui::import_archive(v, archive,
                                  {"", "Secret"},
                                  nullptr, {/*password_protected=*/true, "swordfish"});
    REQUIRE(out.ok);
    CHECK_FALSE(out.needs_password);
    CHECK_EQ(out.imported, 1);

    auto children = v.list("Secret");
    REQUIRE(children.size() == size_t{1});
    crypto::SecureBytes read;
    REQUIRE(v.read_image(*children[0], read) == vault::VaultResult::Ok);
    REQUIRE(read.size() == data.size());
    CHECK(std::memcmp(read.data(), data.data(), data.size()) == 0);

    v.lock();
    cleanup_dir(dir);
}

TEST(archive_import_encrypted_zip_wrong_password_writes_nothing)
{
    // Retried over fresh fixtures for the same reason as
    // archive_reader_extract_failed_needs_password_true_for_wrong_password:
    // traditional ZipCrypto verifies the password against a SINGLE check byte, so
    // a wrong password clears it about 1 run in 256. The verification probe then
    // fails as a decompression error rather than a passphrase one, and the import
    // reports a generic failure (ok = false) instead of needs_password. Measured
    // at 12/3000 = 0.40% against the vendored libarchive — often enough to flake
    // CI, which it did on main (gcc/Release).
    //
    // Nothing is EVER written either way — the probe runs before any vault write,
    // so that invariant is asserted on every attempt. Only the *reason* is
    // probabilistic, so it is asserted across a few independent fixtures: the odds
    // of all five hitting the false-accept are ~1e-12.
    bool saw_needs_password = false;
    for (int attempt = 0; attempt < 5 && !saw_needs_password; ++attempt) {
        auto dir = fresh_dir_local("archive_import_enc_wrong");
        vault::Vault v;
        make_vault(v, dir / "v.osv");

        auto archive = archivetest::make_encrypted_zip(
            {{"one.jpg", fake_bytes(static_cast<uint8_t>(22 + attempt))}}, "swordfish",
            dir / "secret.zip");

        auto out = ui::import_archive(v, archive,
                                      {"", "Secret"},
                                      nullptr, {/*password_protected=*/true, "wrong-guess"});
        CHECK_EQ(out.imported, 0);
        CHECK(v.list("").empty());   // nothing written — no "Secret" gallery exists

        saw_needs_password = out.ok && out.needs_password;

        v.lock();
        cleanup_dir(dir);
    }
    CHECK(saw_needs_password);
}

TEST(archive_import_encrypted_zip_no_password_writes_nothing)
{
    auto dir = fresh_dir_local("archive_import_enc_none");
    vault::Vault v;
    make_vault(v, dir / "v.osv");

    auto archive = archivetest::make_encrypted_zip({{"one.jpg", fake_bytes(23)}}, "swordfish",
                                                    dir / "secret.zip");

    // Simulates the very first attempt, before the user has typed anything.
    auto out = ui::import_archive(v, archive,
                                  {"", "Secret"},
                                  nullptr, {/*password_protected=*/true, ""});
    REQUIRE(out.ok);
    CHECK(out.needs_password);
    CHECK(v.list("").empty());

    v.lock();
    cleanup_dir(dir);
}

TEST(archive_import_cbz_encrypted_zip_correct_password_imports)
{
    auto dir = fresh_dir_local("archive_import_cbz_enc_ok");
    vault::Vault v;
    make_vault(v, dir / "v.osv");

    auto archive = archivetest::make_encrypted_zip(
        {{"2.jpg", fake_bytes(2)}, {"1.jpg", fake_bytes(1)}}, "pw123", dir / "comic.zip");

    auto out = ui::import_archive_cbz(v, archive, "", "Comic", nullptr,
                                      {/*password_protected=*/true, "pw123"});
    REQUIRE(out.ok);
    CHECK_FALSE(out.needs_password);
    CHECK_EQ(out.imported, 2);

    auto children = v.list("Comic");
    REQUIRE(children.size() == size_t{2});
    CHECK_EQ(children[0]->name, "1.jpg");
    CHECK_EQ(children[1]->name, "2.jpg");

    v.lock();
    cleanup_dir(dir);
}

#endif // OSV_VENDORED_ARCHIVE
