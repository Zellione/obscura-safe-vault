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
