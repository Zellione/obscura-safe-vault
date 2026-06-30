#include "test_framework.h"
#include "ui/zip_import.h"
#include "vault/vault.h"
#include "crypto/secure_mem.h"
#include "miniz.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// Synthetic JPEG (valid SOI/APP0 magic so detect_format routes to add_image)
// followed by a seed-derived tail, so each page's decrypted bytes are distinct
// and checkable. Mirrors tests/ui/test_zip_import.cpp.
static std::vector<uint8_t> fake_jpeg(uint8_t seed)
{
    std::vector<uint8_t> v{0xFF, 0xD8, 0xFF, 0xE0};
    for (int i = 0; i < 200; ++i)
        v.push_back(static_cast<uint8_t>(seed + i));
    return v;
}

// Write a .cbz (a plain zip with a comic extension) holding the given entries.
static fs::path make_cbz(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& items,
                         const fs::path& out)
{
    mz_zip_archive z;
    std::memset(&z, 0, sizeof(z));
    mz_zip_writer_init_heap(&z, 0, 0);
    for (const auto& [name, b] : items)
        mz_zip_writer_add_mem(&z, name.c_str(), b.data(), b.size(), MZ_BEST_SPEED);
    void* buf = nullptr;
    size_t sz = 0;
    mz_zip_writer_finalize_heap_archive(&z, &buf, &sz);
    std::ofstream(out, std::ios::binary).write(static_cast<const char*>(buf), static_cast<std::streamsize>(sz));
    mz_free(buf);
    mz_zip_writer_end(&z);
    return out;
}

static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static void make_vault(vault::Vault& v, const fs::path& p)
{
    const std::vector<uint8_t> pw{'p', 'w'};
    (void)vault::Vault::create(p.string(), pw, {}, kTestKdf, v);
}

static fs::path fresh_dir(const char* name)
{
    fs::path dir = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

static void cleanup_dir(const fs::path& dir)
{
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(cbz_import_one_gallery_natural_order_and_checksums)
{
    auto dir = fresh_dir("osv_cbz_order");
    // Added scrambled; expected reading order is 1, 2, 10.
    auto cbz = make_cbz({{"10.jpg", fake_jpeg(10)},
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
    auto cbz = make_cbz({{"ch1/01.jpg", fake_jpeg(1)},
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
    auto cbz = make_cbz({{"1.jpg", fake_jpeg(1)}, {"2.jpg", fake_jpeg(2)}}, dir / "C.cbz");
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
