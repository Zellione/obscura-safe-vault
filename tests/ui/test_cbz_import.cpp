#include "test_framework.h"
#include "ui/zip_import.h"
#include "ui/zip_test_helpers.h"
#include "vault/vault.h"
#include "crypto/secure_mem.h"

#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using ziptest::cleanup_dir;
using ziptest::fake_jpeg;
using ziptest::fresh_dir;
using ziptest::make_archive;
using ziptest::make_vault;

TEST(cbz_import_one_gallery_natural_order_and_checksums)
{
    auto dir = fresh_dir("osv_cbz_order");
    // Added scrambled; expected reading order is 1, 2, 10.
    auto cbz = make_archive({{"10.jpg", fake_jpeg(10)},
                         {"2.jpg", fake_jpeg(2)},
                         {"1.jpg", fake_jpeg(1)},
                         {"notes.txt", {1, 2, 3}},
                         {"clip.mp4", {4, 5, 6}}},
                        dir / "MyComic.cbz");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        auto out = ui::import_cbz(v, cbz, "", "MyComic");
        CHECK(out.ok);
        CHECK_EQ(out.imported, 3);
        CHECK_EQ(out.skipped, 2);   // notes.txt + clip.mp4

        auto pages = v.list("MyComic");
        REQUIRE(pages.size() == static_cast<size_t>(3));
        // Insertion order == natural reading order.
        CHECK_EQ(pages[0]->name, std::string("1.jpg"));
        CHECK_EQ(pages[1]->name, std::string("2.jpg"));
        CHECK_EQ(pages[2]->name, std::string("10.jpg"));

        // Per-page byte-identical readback (the spec's checksum match).
        const std::vector<std::pair<const vault::IndexNode*, uint8_t>> expect{
            {pages[0], 1}, {pages[1], 2}, {pages[2], 10}};
        for (const auto& [node, seed] : expect) {
            crypto::SecureBytes orig;
            REQUIRE(v.read_image(*node, orig) == vault::VaultResult::Ok);
            const auto want = fake_jpeg(seed);
            CHECK_BYTES_EQ((std::span<const uint8_t>{orig.data(), orig.size()}),
                           (std::span<const uint8_t>{want.data(), want.size()}));
        }
    }
    cleanup_dir(dir);
}

TEST(cbz_import_flattens_internal_subfolders)
{
    auto dir = fresh_dir("osv_cbz_flat");
    auto cbz = make_archive({{"ch1/01.jpg", fake_jpeg(1)},
                         {"ch2/01.jpg", fake_jpeg(2)},
                         {"ch1/02.jpg", fake_jpeg(3)}},
                        dir / "Vol.cbz");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        auto out = ui::import_cbz(v, cbz, "", "Vol");
        CHECK(out.ok);
        CHECK_EQ(out.imported, 3);
        // One flat leaf gallery, no sub-galleries.
        auto pages = v.list("Vol");
        CHECK_EQ(pages.size(), static_cast<size_t>(3));
        CHECK(v.list("Vol/ch1").empty());   // not mirrored
    }
    cleanup_dir(dir);
}

TEST(cbz_import_reports_progress)
{
    auto dir = fresh_dir("osv_cbz_progress");
    auto cbz = make_archive({{"1.jpg", fake_jpeg(1)},
                             {"2.jpg", fake_jpeg(2)},
                             {"3.jpg", fake_jpeg(3)}},
                            dir / "P.cbz");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        ui::ImportProgress prog;
        auto out = ui::import_cbz(v, cbz, "", "P", &prog);
        CHECK(out.ok);
        CHECK_EQ(out.imported, 3);
        CHECK_EQ(prog.total.load(), 3);   // set to the page count up front
        CHECK_EQ(prog.done.load(), 3);    // incremented once per stored page
    }
    cleanup_dir(dir);
}

TEST(cbz_import_stops_early_on_cancel)
{
    auto dir = fresh_dir("osv_cbz_cancel");
    auto cbz = make_archive({{"1.jpg", fake_jpeg(1)},
                             {"2.jpg", fake_jpeg(2)},
                             {"3.jpg", fake_jpeg(3)}},
                            dir / "C.cbz");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        ui::ImportProgress prog;
        prog.cancel.store(true);   // pre-cancelled: no page should be imported
        auto out = ui::import_cbz(v, cbz, "", "C", &prog);
        CHECK(out.ok);                       // a cancelled import is still a clean stop
        CHECK_EQ(out.imported, 0);           // cooperative cancel before the first page
        CHECK_EQ(prog.done.load(), 0);
    }
    cleanup_dir(dir);
}

TEST(cbz_import_rejects_malformed_archive)
{
    auto dir = fresh_dir("osv_cbz_bad");
    std::ofstream(dir / "bad.cbz", std::ios::binary) << "this is not a zip";
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        auto out = ui::import_cbz(v, dir / "bad.cbz", "", "Bad");
        CHECK_FALSE(out.ok);
        CHECK_FALSE(out.error.empty());
        CHECK(v.list("Bad").empty());
    }
    cleanup_dir(dir);
}

TEST(cbz_import_writes_nothing_to_disk)
{
    auto dir = fresh_dir("osv_cbz_nofs");
    auto cbz = make_archive({{"1.jpg", fake_jpeg(1)}, {"2.jpg", fake_jpeg(2)}}, dir / "C.cbz");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        auto out = ui::import_cbz(v, cbz, "", "C");
        CHECK(out.ok);
        CHECK_EQ(out.imported, 2);
        // Only the input .cbz and the vault file exist — no decompressed page hit disk.
        int count = 0;
        for (auto& e : fs::directory_iterator(dir)) { (void)e; ++count; }
        CHECK_EQ(count, 2);
    }
    cleanup_dir(dir);
}
