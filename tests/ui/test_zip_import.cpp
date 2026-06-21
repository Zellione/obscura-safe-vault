#include "test_framework.h"
#include "ui/zip_import.h"
#include "vault/vault.h"
#include "crypto/secure_mem.h"
#include "miniz.h"

#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using ui::ZipDest;
using ui::ZipConflictPolicy;

// A synthetic "image" payload: a valid JPEG magic (SOI + APP0) so
// image::detect_format routes it to Vault::add_image, followed by arbitrary
// bytes. The thumbnail decode fails fast on the garbage tail and add_image
// stores the original anyway (thumb_length == 0) — exactly how the existing
// vault tests exercise add_image with synthetic bytes. Real codec decode/
// thumbnailing is covered separately in tests/image; these tests verify the
// import *plumbing* (planner routing, miniz extraction into mlock'd memory,
// add_image storage, byte-identical readback) without coupling to a real codec.
static std::vector<uint8_t> fake_jpeg(uint8_t seed)
{
    std::vector<uint8_t> v{0xFF, 0xD8, 0xFF, 0xE0};
    for (int i = 0; i < 200; ++i)
        v.push_back(static_cast<uint8_t>(seed + i));
    return v;
}

// Build a zip with the given (archive_path -> bytes) entries, return its temp path.
static fs::path make_zip(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& items,
                         const fs::path& out)
{
    mz_zip_archive z;
    std::memset(&z, 0, sizeof(z));
    mz_zip_writer_init_heap(&z, 0, 0);
    for (const auto& [name, bytes] : items)
        mz_zip_writer_add_mem(&z, name.c_str(), bytes.data(), bytes.size(), MZ_BEST_SPEED);
    void* buf = nullptr;
    size_t sz = 0;
    mz_zip_writer_finalize_heap_archive(&z, &buf, &sz);
    std::ofstream(out, std::ios::binary).write(static_cast<const char*>(buf), static_cast<std::streamsize>(sz));
    mz_free(buf);
    mz_zip_writer_end(&z);
    return out;
}

// Fast Argon2 params for tests (mirrors tests/vault/*). Production cost is far higher.
static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};
static std::vector<uint8_t> bytes(std::string_view s)
{
    return {s.begin(), s.end()};
}

// Fill `v` in place. Vault::create is STATIC with the vault as an out-param and
// returns a VaultResult; password/keyfile are byte spans (keyfile {} = none).
static void make_vault(vault::Vault& v, const fs::path& p)
{
    const auto pw = bytes("pw");
    (void)vault::Vault::create(p.string(), pw, {}, kTestKdf, v);  // kTestKdf is valid -> Ok
}

// Fresh empty temp dir. Non-throwing: on Windows a still-open vault file from a
// crashed prior run cannot be deleted, and the throwing overload would abort.
static fs::path fresh_dir(const char* name)
{
    fs::path dir = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

// Remove the temp dir. Non-throwing (matches the TempVault convention in
// tests/vault): the Vault must already be destroyed so its file handle is
// closed — on Windows an open file cannot be deleted, and a throwing remove
// would terminate the whole test process.
static void cleanup_dir(const fs::path& dir)
{
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(zip_import_new_gallery_mirrors_tree)
{
    auto img = fake_jpeg(1);
    auto dir = fresh_dir("osv_zip_test_new");
    auto zip = make_zip({{"2020/winter/a.jpg", img}, {"2020/sub/b.jpg", img}, {"notes.txt", {1, 2, 3}}},
                        dir / "in.zip");

    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        auto out = ui::import_zip(v, zip, ZipDest::NewGallery, "", "Album", ZipConflictPolicy::AskUser);
        CHECK(out.ok);
        CHECK_FALSE(out.needs_resolution);
        CHECK_EQ(out.imported, 2);
        CHECK_EQ(out.skipped, 1);  // notes.txt
        CHECK_EQ(v.list("Album/2020/winter").size(), static_cast<size_t>(1));  // a.jpg
        CHECK_EQ(v.list("Album/2020/sub").size(), static_cast<size_t>(1));  // b.jpg

        // Byte-identical readback (spec: per-file checksum match). Find the imported
        // a.jpg node and compare its decrypted original bytes to the payload.
        const vault::IndexNode* node = nullptr;
        for (const auto* c : v.list("Album/2020/winter"))
            if (c->name == "a.jpg") node = c;
        REQUIRE(node != nullptr);
        crypto::SecureBytes orig;
        REQUIRE(v.read_image(*node, orig) == vault::VaultResult::Ok);
        CHECK_BYTES_EQ((std::span<const uint8_t>{orig.data(), orig.size()}),
                       (std::span<const uint8_t>{img.data(), img.size()}));
    }
    cleanup_dir(dir);
}

TEST(zip_import_append_flattens)
{
    auto img = fake_jpeg(2);
    auto dir = fresh_dir("osv_zip_test_app");
    auto zip = make_zip({{"x/a.jpg", img}, {"x/y/b.jpg", img}}, dir / "in.zip");

    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        (void)v.create_gallery("Leaf");
        auto out = ui::import_zip(v, zip, ZipDest::Append, "Leaf", "", ZipConflictPolicy::AskUser);
        CHECK(out.ok);
        CHECK_EQ(out.imported, 2);
        CHECK_EQ(v.list("Leaf").size(), static_cast<size_t>(2));  // both flattened in
    }
    cleanup_dir(dir);
}

TEST(zip_import_reports_mixed_folder_without_writing)
{
    auto img = fake_jpeg(3);
    auto dir = fresh_dir("osv_zip_test_mix");
    auto zip = make_zip({{"a/x.jpg", img}, {"a/b/y.jpg", img}}, dir / "in.zip");

    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        auto out = ui::import_zip(v, zip, ZipDest::NewGallery, "", "G", ZipConflictPolicy::AskUser);
        CHECK(out.ok);
        CHECK(out.needs_resolution);
        CHECK_EQ(out.imported, 0);
        CHECK(v.list("G").empty());  // nothing written while awaiting resolution
    }
    cleanup_dir(dir);
}

TEST(zip_import_rejects_malformed_archive)
{
    auto dir = fresh_dir("osv_zip_test_bad");
    std::ofstream(dir / "bad.zip", std::ios::binary) << "not a zip at all";

    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        auto out = ui::import_zip(v, dir / "bad.zip", ZipDest::NewGallery, "", "G", ZipConflictPolicy::AskUser);
        CHECK_FALSE(out.ok);
        CHECK_FALSE(out.error.empty());
    }
    cleanup_dir(dir);
}

TEST(zip_import_writes_no_extra_files)
{
    auto img = fake_jpeg(4);
    auto dir = fresh_dir("osv_zip_test_nofs");
    auto zip = make_zip({{"a.jpg", img}}, dir / "in.zip");

    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        auto out = ui::import_zip(v, zip, ZipDest::NewGallery, "", "G", ZipConflictPolicy::AskUser);
        CHECK(out.ok);
        // The only files in `dir` are the input zip and the vault — no decompressed temp.
        int count = 0;
        for (auto& e : fs::directory_iterator(dir)) {
            (void)e;
            ++count;
        }
        CHECK_EQ(count, 2);
    }
    cleanup_dir(dir);
}
