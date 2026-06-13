#include "test_framework.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include "ui/export.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

// Cheap Argon2 params so the suite stays fast (see tests/vault/test_vault.cpp).
static const crypto::KdfParams kExpKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

static std::vector<uint8_t> read_file(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// RAII unique temp directory.
struct TempDir {
    fs::path path;
    explicit TempDir(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_export_" + std::string(tag) + "_" + std::to_string(ctr++));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

// RAII temp .osv path.
struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_exp_vault_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempVault()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
    std::string str() const { return path.string(); }
};

// --- unique_export_path (pure) --------------------------------------------

TEST(export_unique_path_no_collision_returns_plain_name)
{
    auto never = [](const fs::path&) { return false; };
    auto p = ui::unique_export_path("/out", "cat.jpg", never);
    CHECK_EQ(p, fs::path("/out") / "cat.jpg");
}

TEST(export_unique_path_appends_suffix_before_extension)
{
    // "a.jpg" and "a (1).jpg" already exist; the resolver must skip to "a (2).jpg".
    auto exists = [](const fs::path& p) {
        return p.filename() == "a.jpg" || p.filename() == "a (1).jpg";
    };
    auto p = ui::unique_export_path("/out", "a.jpg", exists);
    CHECK_EQ(p.filename().string(), std::string("a (2).jpg"));
}

TEST(export_unique_path_handles_name_without_extension)
{
    auto exists = [](const fs::path& p) { return p.filename() == "README"; };
    auto p = ui::unique_export_path("/out", "README", exists);
    CHECK_EQ(p.filename().string(), std::string("README (1)"));
}

// --- export_one_image: decrypt -> write verbatim -> wipe scratch -----------

TEST(export_one_image_writes_verbatim_and_wipes_buffer)
{
    TempVault tv("one");
    TempDir   out("one");
    auto img = pattern(5000, 7);

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kExpKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", img, "photo.png") == vault::VaultResult::Ok);
    auto kids = v.list("");
    REQUIRE(kids.size() == 1);

    fs::path dest = out.path / "photo.png";
    crypto::SecureBytes scratch;
    REQUIRE(ui::export_one_image(v, *kids[0], dest, scratch) == vault::VaultResult::Ok);

    // File on disk is byte-identical to the originally-imported bytes.
    auto written = read_file(dest);
    CHECK_BYTES_EQ(std::span<const uint8_t>(written), std::span<const uint8_t>(img));

    // The decrypted scratch buffer is wiped (all zero) after the write.
    REQUIRE(scratch.size() == img.size());
    bool all_zero = true;
    for (uint8_t b : scratch.as_span()) all_zero = all_zero && (b == 0);
    CHECK_TRUE(all_zero);
}

// --- export_images: batch, checksum, thumbnails-never, collisions ----------

TEST(export_images_are_byte_identical_and_skip_thumbnails)
{
    TempVault tv("batch");
    TempDir   out("batch");
    auto a = pattern(3000, 1);
    auto b = pattern(4096, 2);

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kExpKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", a, "a.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", b, "b.png") == vault::VaultResult::Ok);
    auto kids = v.list("");
    REQUIRE(kids.size() == 2);

    auto sum = ui::export_images(v, kids, out.path, ui::ExportConsent::Confirm);
    CHECK_EQ(sum.written, 2);
    CHECK_EQ(sum.failed, 0);

    // Exactly two files: the originals, never a thumbnail.
    int file_count = 0;
    for (auto& e : fs::directory_iterator(out.path)) { (void)e; ++file_count; }
    CHECK_EQ(file_count, 2);

    CHECK_BYTES_EQ(std::span<const uint8_t>(read_file(out.path / "a.png")),
                   std::span<const uint8_t>(a));
    CHECK_BYTES_EQ(std::span<const uint8_t>(read_file(out.path / "b.png")),
                   std::span<const uint8_t>(b));
}

TEST(export_images_collision_suffixes_without_overwriting)
{
    TempVault tv("coll");
    TempDir   out("coll");
    auto img = pattern(2000, 9);

    // A pre-existing file occupies the natural destination name.
    {
        std::ofstream f(out.path / "a.png", std::ios::binary);
        const char* existing = "DO NOT OVERWRITE";
        f.write(existing, static_cast<std::streamsize>(std::strlen(existing)));
    }

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kExpKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", img, "a.png") == vault::VaultResult::Ok);
    auto kids = v.list("");

    auto sum = ui::export_images(v, kids, out.path, ui::ExportConsent::Confirm);
    CHECK_EQ(sum.written, 1);

    // The pre-existing file is untouched; the export lands beside it.
    CHECK_EQ(read_file(out.path / "a.png").size(), std::strlen("DO NOT OVERWRITE"));
    CHECK_BYTES_EQ(std::span<const uint8_t>(read_file(out.path / "a (1).png")),
                   std::span<const uint8_t>(img));
}

TEST(export_images_declining_writes_nothing)
{
    TempVault tv("decline");
    TempDir   out("decline");

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kExpKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(100, 1), "a.png") == vault::VaultResult::Ok);
    auto kids = v.list("");

    auto sum = ui::export_images(v, kids, out.path, ui::ExportConsent::Cancel);
    CHECK_EQ(sum.written, 0);

    int file_count = 0;
    for (auto& e : fs::directory_iterator(out.path)) { (void)e; ++file_count; }
    CHECK_EQ(file_count, 0);
}
