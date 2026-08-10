#include "test_framework.h"

#ifdef OSV_VENDORED_ARCHIVE

#include "ui/archive_import.h"
#include "archive_test_helpers.h"
#include "platform/path_utf8.h"
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

// --- Phase 72: CJK names ----------------------------------------------------
//
// 7z stores entry names as UTF-16 in the header. On Windows libarchive's
// narrow archive_entry_pathname() renders them through the ANSI code page and
// returns NULL when a CJK name has no mapping — pre-fix, every such entry
// collapsed to the "unnamed" fallback. The reader must take the UTF-8 name.
//
// The fixture is a committed real-7-Zip archive (uuencoded, like the RAR
// multivolume fixtures): libarchive's own 7z WRITER converts names through
// the process locale and cannot produce CJK names under "C", so the writer
// helper cannot build this fixture at test time. Contents: 写真/風景.jpg
// (= fake_bytes(1)) and 图片.jpg (= fake_bytes(2)) → imported into 相册.
TEST(archive_import_7z_cjk_entry_names)
{
    auto dir = fresh_dir_local("archive_import_7z_cjk");
    vault::Vault v;
    make_vault(v, dir / "v.osv");

    const std::filesystem::path fixture_dir = OSV_UI_FIXTURE_DIR;
    const std::filesystem::path archive = dir / "cjk.7z";
    archivetest::uudecode(fixture_dir / "cjk_names.7z.uu", archive);

    auto out = ui::import_archive(v, archive, {"", "\xE7\x9B\xB8\xE5\x86\x8C"});
    REQUIRE(out.ok);
    CHECK_EQ(out.imported, 2);
    CHECK_EQ(v.list("\xE7\x9B\xB8\xE5\x86\x8C/\xE5\x86\x99\xE7\x9C\x9F").size(), size_t{1});

    bool found = false;
    for (const auto* c : v.list("\xE7\x9B\xB8\xE5\x86\x8C"))
        if (c->name == "\xE5\x9B\xBE\xE7\x89\x87.jpg") found = true;
    CHECK(found);

    v.lock();
    cleanup_dir(dir);
}

// ustar carries entry names as raw bytes; UTF-8 names must round-trip
// byte-identically into vault node names on every platform.
TEST(archive_import_tar_cjk_entry_names)
{
    auto dir = fresh_dir_local("archive_import_tar_cjk");
    vault::Vault v;
    make_vault(v, dir / "v.osv");

    auto archive = make_archive(
        {{"\xE5\x86\x99\xE7\x9C\x9F/\xE9\xA2\xA8\xE6\x99\xAF.jpg", fake_bytes(3)}},
        "ustar", dir / "cjk.tar");

    auto out = ui::import_archive(v, archive, {"", "\xE7\x9B\xB8\xE5\x86\x8C"});
    REQUIRE(out.ok);
    CHECK_EQ(out.imported, 1);

    bool found = false;
    for (const auto* c : v.list("\xE7\x9B\xB8\xE5\x86\x8C/\xE5\x86\x99\xE7\x9C\x9F"))
        if (c->name == "\xE9\xA2\xA8\xE6\x99\xAF.jpg") found = true;
    CHECK(found);

    v.lock();
    cleanup_dir(dir);
}

// The archive FILE itself carries a CJK name (漫画.tar): the import must open
// it through the native path range, never the ANSI code page.
TEST(archive_import_cjk_archive_filename)
{
    auto dir = fresh_dir_local("archive_import_cjk_name");
    vault::Vault v;
    make_vault(v, dir / "v.osv");

    // Write under an ASCII name, then rename — the fixture writer's own path
    // handling (archive_write_open_filename is narrow) is not under test.
    auto ascii = make_archive({{"01.jpg", fake_bytes(4)}}, "ustar", dir / "in.tar");
    const std::filesystem::path archive =
        dir / platform::utf8_to_path("\xE6\xBC\xAB\xE7\x94\xBB.tar");
    std::filesystem::rename(ascii, archive);

    auto out = ui::import_archive(v, archive, {"", "Comics"});
    REQUIRE(out.ok);
    CHECK_EQ(out.imported, 1);
    CHECK_EQ(v.list("Comics").size(), size_t{1});

    v.lock();
    cleanup_dir(dir);
}

#endif // OSV_VENDORED_ARCHIVE
