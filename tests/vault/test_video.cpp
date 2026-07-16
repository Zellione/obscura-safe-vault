#include "test_framework.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "crypto/secure_mem.h"
#include "vault/vault.h"
#include "vault/index.h"
#include "vault/vault_ops.h"

namespace vault {
// Test-only seam — see the friend declaration + comment in vault.h.
void test_only_force_video_codec_unknown(Vault& v, std::string_view node_path)
{
    IndexNode* n = v.resolve_node(node_path);
    if (!n) return;
    n->vmeta.codec         = VideoCodec::Unknown;
    n->vmeta.width         = 0;
    n->vmeta.height        = 0;
    n->vmeta.duration_us   = 0;
    n->vmeta.poster_offset = 0;
    n->vmeta.poster_length = 0;
}
}  // namespace vault

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

#ifdef OSV_VENDORED_AV
    // On FFmpeg-enabled builds, verify that add_video populated the metadata.
    CHECK_EQ(node.vmeta.width, 160);
    CHECK_EQ(node.vmeta.height, 120);
    CHECK_EQ(static_cast<int>(node.vmeta.codec), static_cast<int>(vault::VideoCodec::H264));
    CHECK(node.vmeta.duration_us > 0);

    // Verify the poster was stored.
    crypto::SecureBytes poster;
    REQUIRE(v2.read_thumbnail(node, poster) == vault::VaultResult::Ok);
    CHECK(!poster.empty());
#endif

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

// Regression test for a real crash: appending the new IndexNode to a
// gallery's children (std::vector::push_back) can throw bad_alloc on
// allocation failure. Uncaught, that calls std::terminate() and kills the
// whole process instead of returning an error — the same bug class fixed for
// chunk_codec::resize_buf (PR #57), but at add_video's tree-mutation step.
TEST(add_video_vector_push_failure_returns_io_error_not_terminate)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video_bytes.empty());

    TempVault tv("video_push_fail");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("clips") == vault::VaultResult::Ok);

    vault::vault_ops::inject_push_child_failure(0);   // fail the very next push_child
    CHECK_EQ(v.add_video("clips", video_bytes, "a.mp4", 4096), vault::VaultResult::IoError);
    vault::vault_ops::clear_push_child_failure();

    // The vault must still work normally afterward (fault disarms after firing).
    REQUIRE(v.add_video("clips", video_bytes, "b.mp4", 4096) == vault::VaultResult::Ok);
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

TEST(read_thumbnail_video_with_poster)
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
#ifdef OSV_VENDORED_AV
    // On FFmpeg-enabled builds, we generate a poster from the first frame.
    CHECK(v.read_thumbnail(*children[0], out) == vault::VaultResult::Ok);
    CHECK(!out.empty());
#else
    // On non-FFmpeg builds, there's no poster.
    CHECK(v.read_thumbnail(*children[0], out) == vault::VaultResult::NotFound);
#endif
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

// --- repair_video_metadata (Phase 40 bugfix) --------------------------------

TEST(repair_video_metadata_returns_locked_when_vault_locked)
{
    TempVault tv("repair_locked");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    v.lock();
    CHECK(v.repair_video_metadata("anything.mp4") == vault::VaultResult::Locked);
}

TEST(repair_video_metadata_returns_not_found_for_missing_path)
{
    TempVault tv("repair_missing");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    CHECK(v.repair_video_metadata("does/not/exist.mp4") == vault::VaultResult::NotFound);
}

TEST(repair_video_metadata_returns_not_found_for_non_video_node)
{
    auto image_bytes = read_file(OSV_FIXTURE_DIR "/sample.webp");
    REQUIRE(!image_bytes.empty());

    TempVault tv("repair_non_video");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", image_bytes, "i.webp") == vault::VaultResult::Ok);
    CHECK(v.repair_video_metadata("i.webp") == vault::VaultResult::NotFound);
}

#ifdef OSV_VENDORED_AV

TEST(repair_video_metadata_is_noop_when_codec_already_known)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video_bytes.empty());

    TempVault tv("repair_noop_known");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_video("", video_bytes, "tiny.mp4", 4096) == vault::VaultResult::Ok);

    auto before = v.list("");
    REQUIRE(before.size() == 1);
    REQUIRE(static_cast<int>(before[0]->vmeta.codec) !=
            static_cast<int>(vault::VideoCodec::Unknown));
    const auto codec_before    = before[0]->vmeta.codec;
    const auto duration_before = before[0]->vmeta.duration_us;
    const auto poster_off_before = before[0]->vmeta.poster_offset;
    const auto poster_len_before = before[0]->vmeta.poster_length;

    CHECK(v.repair_video_metadata("tiny.mp4") == vault::VaultResult::Ok);

    auto after = v.list("");
    REQUIRE(after.size() == 1);
    CHECK(static_cast<int>(after[0]->vmeta.codec) == static_cast<int>(codec_before));
    CHECK(after[0]->vmeta.duration_us == duration_before);
    CHECK(after[0]->vmeta.poster_offset == poster_off_before);
    CHECK(after[0]->vmeta.poster_length == poster_len_before);
}

TEST(repair_video_metadata_leaves_still_undecodable_video_unchanged)
{
    // A codec our build genuinely cannot decode (mpeg2video isn't in the
    // vendored FFmpeg's --enable-decoder list) — add_video() still accepts
    // it (container detected via EBML magic) but stores codec=Unknown,
    // duration=0, no poster. repair_video_metadata() must leave it exactly
    // as-is (Ok, no-op) rather than erroring, since re-probing genuinely
    // still fails.
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/undecodable_mpeg2.mkv");
    REQUIRE(!video_bytes.empty());

    TempVault tv("repair_still_undecodable");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_video("", video_bytes, "undecodable_mpeg2.mkv", 4096) == vault::VaultResult::Ok);

    auto before = v.list("");
    REQUIRE(before.size() == 1);
    REQUIRE(static_cast<int>(before[0]->vmeta.codec) ==
            static_cast<int>(vault::VideoCodec::Unknown));
    REQUIRE(before[0]->vmeta.duration_us == 0);
    REQUIRE(before[0]->vmeta.poster_length == 0);

    CHECK(v.repair_video_metadata("undecodable_mpeg2.mkv") == vault::VaultResult::Ok);

    auto after = v.list("");
    REQUIRE(after.size() == 1);
    CHECK(static_cast<int>(after[0]->vmeta.codec) == static_cast<int>(vault::VideoCodec::Unknown));
    CHECK(after[0]->vmeta.duration_us == 0);
    CHECK(after[0]->vmeta.poster_length == 0);
}

TEST(repair_video_metadata_fills_in_codec_duration_and_poster_when_now_decodable)
{
    // Simulates the real Phase 40 bug: a video whose codec wasn't decodable
    // at import time (here, forced via the test-only seam, standing in for
    // "this build's FFmpeg didn't have AV1 support yet") gets its codec,
    // duration, and poster filled in once repair_video_metadata() re-probes
    // it against a build that CAN decode it — without touching the original
    // stored bytes.
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video_bytes.empty());

    TempVault tv("repair_fills_in");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_video("", video_bytes, "tiny.mp4", 4096) == vault::VaultResult::Ok);

    vault::test_only_force_video_codec_unknown(v, "tiny.mp4");

    auto before = v.list("");
    REQUIRE(before.size() == 1);
    REQUIRE(static_cast<int>(before[0]->vmeta.codec) ==
            static_cast<int>(vault::VideoCodec::Unknown));
    REQUIRE(before[0]->vmeta.duration_us == 0);
    REQUIRE(before[0]->vmeta.poster_length == 0);

    CHECK(v.repair_video_metadata("tiny.mp4") == vault::VaultResult::Ok);

    auto after = v.list("");
    REQUIRE(after.size() == 1);
    CHECK(static_cast<int>(after[0]->vmeta.codec) == static_cast<int>(vault::VideoCodec::H264));
    CHECK(after[0]->vmeta.duration_us > 0);
    CHECK(after[0]->vmeta.poster_length > 0);

    // The original bytes are untouched — chunks/orig_size weren't rewritten.
    crypto::SecureBytes out;
    REQUIRE(v.read_video(*after[0], out) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(video_bytes));
}

#endif  // OSV_VENDORED_AV
