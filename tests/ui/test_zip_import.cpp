#include "test_framework.h"
#include "ui/zip_import.h"
#include "vault/vault.h"
#include "crypto/secure_mem.h"
#include "miniz.h"

#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using ui::ZipDest;
using ui::ZipConflictPolicy;

static std::vector<uint8_t> read_fixture(const char* rel)
{
    std::ifstream f(std::string(OSV_FIXTURE_DIR) + "/" + rel, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
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

TEST(zip_import_new_gallery_mirrors_tree)
{
    auto webp = read_fixture("sample.webp");
    auto dir = fs::temp_directory_path() / "osv_zip_test_new";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto zip = make_zip({{"2020/winter/a.webp", webp}, {"2020/sub/b.webp", webp}, {"notes.txt", {1, 2, 3}}},
                        dir / "in.zip");

    vault::Vault v;
    make_vault(v, dir / "v.osv");
    auto out = ui::import_zip(v, zip, ZipDest::NewGallery, "", "Album", ZipConflictPolicy::AskUser);
    CHECK(out.ok);
    CHECK_FALSE(out.needs_resolution);
    CHECK_EQ(out.imported, 2);
    CHECK_EQ(out.skipped, 1);  // notes.txt
    CHECK_EQ(v.list("Album/2020/winter").size(), static_cast<size_t>(1));  // a.webp
    CHECK_EQ(v.list("Album/2020/sub").size(), static_cast<size_t>(1));  // b.webp

    // Byte-identical readback (spec: per-file checksum match). Find the imported
    // a.webp node and compare its decrypted original bytes to the fixture.
    const vault::IndexNode* img = nullptr;
    for (const auto* c : v.list("Album/2020/winter"))
        if (c->name == "a.webp") img = c;
    REQUIRE(img != nullptr);
    crypto::SecureBytes orig;
    REQUIRE(v.read_image(*img, orig) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ((std::span<const uint8_t>{orig.data(), orig.size()}),
                   (std::span<const uint8_t>{webp.data(), webp.size()}));

    fs::remove_all(dir);
}

TEST(zip_import_append_flattens)
{
    auto webp = read_fixture("sample.webp");
    auto dir = fs::temp_directory_path() / "osv_zip_test_app";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto zip = make_zip({{"x/a.webp", webp}, {"x/y/b.webp", webp}}, dir / "in.zip");

    vault::Vault v;
    make_vault(v, dir / "v.osv");
    (void)v.create_gallery("Leaf");
    auto out = ui::import_zip(v, zip, ZipDest::Append, "Leaf", "", ZipConflictPolicy::AskUser);
    CHECK(out.ok);
    CHECK_EQ(out.imported, 2);
    CHECK_EQ(v.list("Leaf").size(), static_cast<size_t>(2));  // both flattened in
    fs::remove_all(dir);
}

TEST(zip_import_reports_mixed_folder_without_writing)
{
    auto webp = read_fixture("sample.webp");
    auto dir = fs::temp_directory_path() / "osv_zip_test_mix";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto zip = make_zip({{"a/x.webp", webp}, {"a/b/y.webp", webp}}, dir / "in.zip");

    vault::Vault v;
    make_vault(v, dir / "v.osv");
    auto out = ui::import_zip(v, zip, ZipDest::NewGallery, "", "G", ZipConflictPolicy::AskUser);
    CHECK(out.ok);
    CHECK(out.needs_resolution);
    CHECK_EQ(out.imported, 0);
    CHECK(v.list("G").empty());  // nothing written while awaiting resolution
    fs::remove_all(dir);
}

TEST(zip_import_rejects_malformed_archive)
{
    auto dir = fs::temp_directory_path() / "osv_zip_test_bad";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "bad.zip", std::ios::binary) << "not a zip at all";

    vault::Vault v;
    make_vault(v, dir / "v.osv");
    auto out = ui::import_zip(v, dir / "bad.zip", ZipDest::NewGallery, "", "G", ZipConflictPolicy::AskUser);
    CHECK_FALSE(out.ok);
    CHECK_FALSE(out.error.empty());
    fs::remove_all(dir);
}

TEST(zip_import_writes_no_extra_files)
{
    auto webp = read_fixture("sample.webp");
    auto dir = fs::temp_directory_path() / "osv_zip_test_nofs";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto zip = make_zip({{"a.webp", webp}}, dir / "in.zip");
    auto vpath = dir / "v.osv";
    vault::Vault v;
    make_vault(v, vpath);

    auto out = ui::import_zip(v, zip, ZipDest::NewGallery, "", "G", ZipConflictPolicy::AskUser);
    CHECK(out.ok);
    // The only files in `dir` are the input zip and the vault — no decompressed temp.
    int count = 0;
    for (auto& e : fs::directory_iterator(dir)) {
        (void)e;
        ++count;
    }
    CHECK_EQ(count, 2);
    fs::remove_all(dir);
}
