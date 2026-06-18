#include "test_framework.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "crypto/secure_mem.h"
#include "vault/vault.h"
#include "vault/index.h"

namespace {

// Test KDF params: cheap Argon2 so the test suite stays fast.
static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

// RAII temp path for a unique .osv file.
struct TempVault {
    std::filesystem::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = std::filesystem::temp_directory_path() /
               ("osv_test_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    ~TempVault()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    std::string str() const { return path.string(); }
};

// Read a file into a vector.
std::vector<uint8_t> read_file(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// Convert std::string to span of bytes for password.
static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

} // namespace

TEST(add_video_round_trips_container_checksum)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video_bytes.empty());

    // Create a temp vault unlocked.
    TempVault tv("video_roundtrip");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    // Create a gallery and add the video with a small chunk_size to force multi-chunk.
    REQUIRE(v.create_gallery("clips") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("clips", video_bytes, "tiny.mp4", /*chunk_size=*/4096)
            == vault::VaultResult::Ok);

    // Lock and reopen the vault.
    v.lock();
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    // Verify the stored video metadata.
    auto children = v2.list("clips");
    REQUIRE(children.size() == 1);
    const vault::IndexNode& node = *children[0];
    CHECK(node.is_video());
    CHECK_EQ(static_cast<int>(node.vmeta.container), static_cast<int>(vault::VideoContainer::MP4));
    CHECK_EQ(node.vmeta.orig_size, video_bytes.size());
    const size_t expect_chunks = (video_bytes.size() + 4096 - 1) / 4096;
    CHECK_EQ(node.vmeta.chunks.size(), expect_chunks);

    // Read the video back and verify the checksum.
    crypto::SecureBytes out;
    REQUIRE(v2.read_video(node, out) == vault::VaultResult::Ok);
    REQUIRE(out.size() == video_bytes.size());
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(video_bytes));
}

TEST(add_video_rejects_non_container)
{
    std::vector<uint8_t> junk(64, 0xAB);  // no MP4/MKV magic

    TempVault tv("video_badmagic");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("clips") == vault::VaultResult::Ok);

    CHECK(v.add_video("clips", junk, "bad.bin") == vault::VaultResult::InvalidArg);
}

TEST(add_video_on_locked_vault_returns_locked)
{
    TempVault tv("video_locked");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        // Create a gallery and leave it for the next scope (will be locked).
    }

    // Reopen and keep locked.
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    CHECK_FALSE(v2.is_unlocked());

    std::vector<uint8_t> any = {0x1A, 0x45, 0xDF, 0xA3};  // MKV magic
    CHECK(v2.add_video("", any, "x.mkv") == vault::VaultResult::Locked);
}

TEST(add_video_matroska)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mkv");
    REQUIRE(!video_bytes.empty());

    TempVault tv("video_mkv");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    REQUIRE(v.add_video("", video_bytes, "tiny.mkv", /*chunk_size=*/4096)
            == vault::VaultResult::Ok);

    auto children = v.list("");
    REQUIRE(children.size() == 1);
    const vault::IndexNode& node = *children[0];
    CHECK(node.is_video());
    CHECK_EQ(static_cast<int>(node.vmeta.container), static_cast<int>(vault::VideoContainer::MKV));

    crypto::SecureBytes out;
    REQUIRE(v.read_video(node, out) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(video_bytes));
}

TEST(search_includes_video_leaves)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video_bytes.empty());

    TempVault tv("search_video");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    REQUIRE(v.create_gallery("clips") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("clips", video_bytes, "sunset.mp4", 4096) == vault::VaultResult::Ok);

    auto hits = v.search("sunset", vault::SearchScope::Images);
    bool found = false;
    for (const auto& h : hits) {
        if (h.name == "sunset.mp4") { found = true; break; }
    }
    CHECK(found);
}

TEST(favorite_video_appears_in_favorites)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video_bytes.empty());

    TempVault tv("favorite_video");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    REQUIRE(v.create_gallery("clips") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("clips", video_bytes, "a.mp4", 4096) == vault::VaultResult::Ok);
    REQUIRE(v.toggle_favorite("clips/a.mp4") == vault::VaultResult::Ok);

    auto favs = v.list_favorite_images();
    bool found = false;
    for (const auto& h : favs) {
        if (h.name == "a.mp4") { found = true; break; }
    }
    CHECK(found);
}

TEST(read_thumbnail_video_without_poster_is_not_found)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video_bytes.empty());

    TempVault tv("video_thumbnail");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    REQUIRE(v.create_gallery("clips") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("clips", video_bytes, "a.mp4", 4096) == vault::VaultResult::Ok);

    auto children = v.list("clips");
    REQUIRE(children.size() == 1);
    REQUIRE(children[0]->is_video());
    crypto::SecureBytes out;
    CHECK(v.read_thumbnail(*children[0], out) == vault::VaultResult::NotFound);
}

TEST(leaf_gallery_accepts_mixed_image_and_video)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    auto image_bytes = read_file(OSV_FIXTURE_DIR "/sample.webp");
    REQUIRE(!video_bytes.empty());
    REQUIRE(!image_bytes.empty());

    TempVault tv("mixed_media");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    REQUIRE(v.create_gallery("mix") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("mix", video_bytes, "v.mp4", 4096) == vault::VaultResult::Ok);
    CHECK(v.add_image("mix", image_bytes, "i.webp") == vault::VaultResult::Ok);
}
