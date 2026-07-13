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

    auto out = ui::import_archive(v, archive, ui::ZipDest::NewGallery, "", "Book",
                                  ui::ZipConflictPolicy::FlattenMixed);
    REQUIRE(out.ok);
    CHECK_FALSE(out.needs_resolution);
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

    auto out = ui::import_archive(v, archive, ui::ZipDest::NewGallery, "", "Gal",
                                  ui::ZipConflictPolicy::FlattenMixed);
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

    auto out = ui::import_archive(v, archive, ui::ZipDest::NewGallery, "", "Vol",
                                  ui::ZipConflictPolicy::FlattenMixed);
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

    auto out = ui::import_archive(v, bad, ui::ZipDest::NewGallery, "", "Gal",
                                  ui::ZipConflictPolicy::FlattenMixed);
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

    auto out = ui::import_archive(v, archive, ui::ZipDest::NewGallery, "", "Gal",
                                  ui::ZipConflictPolicy::FlattenMixed);
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

#endif // OSV_VENDORED_ARCHIVE
