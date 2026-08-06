#include "test_framework.h"

#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include "vault/staging.h"
#include "vault/transfer.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

static const crypto::KdfParams kKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

// Internal linkage: several test files each define their own `TempVault`
// with a DIFFERENT layout. At namespace scope those are one-definition-rule
// violations — the member functions are implicitly inline, so the linker keeps
// a single copy and silently discards the rest.
namespace {

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_prestg_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec; fs::remove(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove(path, ec); }
    std::string str() const { return path.string(); }
};

}  // namespace

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 31 + seed);
    return v;
}

// Find a media (image or video) node by name in a gallery; nullptr if absent.
[[maybe_unused]] static const vault::IndexNode* find_image(const vault::Vault& v, std::string_view gallery,
                                          std::string_view name)
{
    for (const auto* c : v.list(gallery))
        if (c->is_media() && c->name == name) return c;
    return nullptr;
}

// Precomputed info must bypass probe_video entirely: these bytes are not a
// decodable video, so the probe path would return InvalidArg.
TEST(stage_video_precomputed_skips_probe)
{
    using enum vault::VaultResult;
    TempVault tv("v1");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);

    const auto garbage = pattern(3u << 20, 9);   // 3 MiB of non-video bytes

    vault::StagedVideoInfo info;
    info.poster_jpeg = {0xFF, 0xD8, 0xFF, 0xE0, 1, 2, 3, 4};
    info.container   = vault::VideoContainer::MKV;
    info.codec       = vault::VideoCodec::Unknown;   // legacy carry-through
    info.width       = 640;
    info.height      = 360;
    info.duration_us = 1234567;

    vault::StagedNode staged =
        vault::stage_video(v, garbage, "clip.mkv", vault::VIDEO_CHUNK_SIZE, &info);
    REQUIRE(staged.status == Ok);
    CHECK(staged.node.vmeta.container == vault::VideoContainer::MKV);
    CHECK(staged.node.vmeta.codec == vault::VideoCodec::Unknown);
    CHECK_EQ(staged.node.vmeta.width, 640u);
    CHECK_EQ(staged.node.vmeta.height, 360u);
    CHECK_EQ(staged.node.vmeta.duration_us, 1234567u);
    CHECK(staged.node.vmeta.poster_length > 0);      // poster blob was stored
    CHECK_EQ(staged.node.vmeta.orig_size, garbage.size());
}

// Empty poster_jpeg => no poster chunk, poster span stays 0.
TEST(stage_video_precomputed_empty_poster)
{
    using enum vault::VaultResult;
    TempVault tv("v2");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);

    vault::StagedVideoInfo info;   // poster_jpeg empty, all fields default
    vault::StagedNode staged =
        vault::stage_video(v, pattern(4096, 1), "p.mp4", vault::VIDEO_CHUNK_SIZE, &info);
    REQUIRE(staged.status == Ok);
    CHECK_EQ(staged.node.vmeta.poster_length, 0u);
}

// Regression guard: WITHOUT precomputed info the probe still rejects garbage.
TEST(stage_video_without_precomputed_still_probes)
{
    using enum vault::VaultResult;
    TempVault tv("v3");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);
    vault::StagedNode staged =
        vault::stage_video(v, pattern(4096, 2), "bad.mp4", vault::VIDEO_CHUNK_SIZE);
    CHECK(staged.status == InvalidArg);
}

// add_image_prestaged must store the given thumbnail bytes VERBATIM (no decode)
// and honour created_ts. Unknown format + undecodable bytes must be accepted.
TEST(add_image_prestaged_carries_thumb_and_ts)
{
    using enum vault::VaultResult;
    TempVault tv("v4");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);

    vault::StagedThumb thumb;
    thumb.thumb_jpeg = {0xFF, 0xD8, 9, 8, 7, 6};
    thumb.format     = vault::ImageFormat::Unknown;
    thumb.width      = 123;
    thumb.height     = 45;
    thumb.animated   = true;

    REQUIRE(vault::add_image_prestaged(v, "", pattern(2000, 5), "x.bin", thumb,
                                       /*created_ts=*/424242) == Ok);

    const auto* n = find_image(v, "", "x.bin");
    REQUIRE(n != nullptr);
    CHECK(n->meta.format == vault::ImageFormat::Unknown);
    CHECK_EQ(n->meta.width, 123u);
    CHECK_EQ(n->meta.height, 45u);
    CHECK(n->meta.animated);
    CHECK_EQ(n->meta.created_ts, 424242u);

    crypto::SecureBytes tb;
    REQUIRE(v.read_thumbnail(*n, tb) == Ok);
    CHECK_BYTES_EQ(tb.as_span(), std::span<const uint8_t>(thumb.thumb_jpeg));
}

// add_video_prestaged: Unknown codec + garbage bytes accepted, poster verbatim,
// created_ts preserved, and everything survives a reopen.
TEST(add_video_prestaged_roundtrip_across_reopen)
{
    using enum vault::VaultResult;
    TempVault tv("v5");
    const auto data = pattern(2u << 20, 6);
    vault::StagedVideoInfo info;
    info.poster_jpeg = {0xFF, 0xD8, 1, 2, 3};
    info.codec       = vault::VideoCodec::Unknown;
    info.container   = vault::VideoContainer::Unknown;
    info.width = 320; info.height = 240; info.duration_us = 99;

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);
        REQUIRE(vault::add_video_prestaged(v, "", data, "old.avi", info, 777) == Ok);
    }
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == Ok);
    const auto* n = find_image(v2, "", "old.avi");
    REQUIRE(n != nullptr);
    CHECK(n->vmeta.codec == vault::VideoCodec::Unknown);
    CHECK_EQ(n->vmeta.created_ts, 777u);
    crypto::SecureBytes out;
    REQUIRE(v2.read_video(*n, out) == Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(data));
    crypto::SecureBytes poster;
    REQUIRE(v2.read_thumbnail(*n, poster) == Ok);
    CHECK_BYTES_EQ(poster.as_span(), std::span<const uint8_t>(info.poster_jpeg));
}

// Prestaged adds still enforce the vault ingress boundary (safe name, collision).
TEST(add_image_prestaged_validates_name_and_collision)
{
    using enum vault::VaultResult;
    TempVault tv("v6");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);
    const vault::StagedThumb none;
    CHECK(vault::add_image_prestaged(v, "", pattern(100, 1), "bad<name>", none, 0)
          == InvalidArg);
    REQUIRE(vault::add_image_prestaged(v, "", pattern(100, 1), "a.jpg", none, 0) == Ok);
    CHECK(vault::add_image_prestaged(v, "", pattern(100, 2), "a.jpg", none, 0)
          == AlreadyExists);
}

// The Phase 67 headline: a legacy video (codec Unknown — Phase 65 migration
// legitimately skips undecodable ones) must transfer instead of being re-probed
// and rejected with InvalidArg at the destination.
TEST(transfer_unknown_codec_video_succeeds)
{
    using enum vault::VaultResult;
    TempVault sa("v7s"), da("v7d");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);

    vault::StagedVideoInfo info;
    info.codec = vault::VideoCodec::Unknown;
    info.poster_jpeg = {0xFF, 0xD8, 5, 5};
    REQUIRE(vault::add_video_prestaged(src, "", pattern(1u << 20, 3), "legacy.avi",
                                       info, 1111) == Ok);

    REQUIRE(vault::transfer_image(src, "", "legacy.avi", dst, "",
                                  vault::TransferMode::Move) == Ok);
    const auto* moved = find_image(dst, "", "legacy.avi");
    REQUIRE(moved != nullptr);
    CHECK(moved->vmeta.codec == vault::VideoCodec::Unknown);
    CHECK_EQ(moved->vmeta.created_ts, 1111u);         // timestamp preserved
    crypto::SecureBytes poster;
    REQUIRE(dst.read_thumbnail(*moved, poster) == Ok); // poster carried verbatim
    CHECK_BYTES_EQ(poster.as_span(), std::span<const uint8_t>(info.poster_jpeg));
    CHECK(find_image(src, "", "legacy.avi") == nullptr);
}

// Image transfer copies the stored thumbnail bytes instead of re-decoding: an
// Unknown-format image (undecodable) keeps its thumb and metadata across the move.
TEST(transfer_image_carries_thumb_verbatim)
{
    using enum vault::VaultResult;
    TempVault sa("v8s"), da("v8d");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);

    vault::StagedThumb thumb;
    thumb.thumb_jpeg = {0xFF, 0xD8, 7, 7, 7};
    thumb.format = vault::ImageFormat::Unknown;
    thumb.width = 11; thumb.height = 22; thumb.animated = false;
    REQUIRE(vault::add_image_prestaged(src, "", pattern(5000, 4), "odd.bin",
                                       thumb, 2222) == Ok);

    REQUIRE(vault::transfer_image(src, "", "odd.bin", dst, "",
                                  vault::TransferMode::Copy) == Ok);
    const auto* copied = find_image(dst, "", "odd.bin");
    REQUIRE(copied != nullptr);
    CHECK(copied->meta.format == vault::ImageFormat::Unknown);
    CHECK_EQ(copied->meta.width, 11u);
    CHECK_EQ(copied->meta.created_ts, 2222u);
    crypto::SecureBytes tb;
    REQUIRE(dst.read_thumbnail(*copied, tb) == Ok);
    CHECK_BYTES_EQ(tb.as_span(), std::span<const uint8_t>(thumb.thumb_jpeg));
}

// A source item without a thumbnail transfers without one (no regeneration).
TEST(transfer_image_no_thumb_stays_no_thumb)
{
    using enum vault::VaultResult;
    TempVault sa("v9s"), da("v9d");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    const vault::StagedThumb none;   // empty thumb_jpeg
    REQUIRE(vault::add_image_prestaged(src, "", pattern(700, 8), "n.bin", none, 0) == Ok);

    REQUIRE(vault::transfer_image(src, "", "n.bin", dst, "",
                                  vault::TransferMode::Copy) == Ok);
    const auto* copied = find_image(dst, "", "n.bin");
    REQUIRE(copied != nullptr);
    CHECK_EQ(copied->meta.thumb_length, 0u);
}
