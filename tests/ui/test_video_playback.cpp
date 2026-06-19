#include "test_framework.h"

// VideoPlayback wires the decoder + transport model + YUV texture together. The
// pure transport maths is covered by test_playback_model; this drives the whole
// glue against a headless software renderer over a real encrypted fixture, and
// asserts the security invariant that playback writes nothing to disk. Gated on
// the vendored FFmpeg build (valid() is always false without it).
#ifdef OSV_VENDORED_AV

#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "crypto/kdf.h"
#include "gfx/renderer.h"
#include "gfx/text.h"
#include "ui/video_playback.h"
#include "vault/index.h"
#include "vault/vault.h"

namespace {
namespace fs = std::filesystem;

static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_vp_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
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

std::vector<uint8_t> read_file(const char* file_path)
{
    std::ifstream f(file_path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

const vault::IndexNode* first_video(const std::vector<const vault::IndexNode*>& ns)
{
    for (const vault::IndexNode* n : ns)
        if (n->is_video()) return n;
    return nullptr;
}
}  // namespace

TEST(video_playback_opens_plays_seeks_and_writes_no_disk)
{
    auto vbytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!vbytes.empty());

    TempVault tv("play");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", vbytes, "tiny.mp4", 4096) == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_video(v.list("c"));
    REQUIRE(node != nullptr);

    // Snapshot the .osv size: a full open/play/seek cycle must not write a byte.
    std::error_code ec;
    const auto size_before = fs::file_size(tv.path, ec);
    REQUIRE(!ec);

    // Headless software renderer + a baked font (matching the gfx tests).
    SDL_Surface* surf = SDL_CreateSurface(320, 240, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(surf != nullptr);
    SDL_Renderer* sr = SDL_CreateSoftwareRenderer(surf);
    REQUIRE(sr != nullptr);
    gfx::Renderer r(sr);
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(OSV_DEFAULT_FONT, 18.0f));

    {
        ui::VideoPlayback vp(v, *node);
        REQUIRE(vp.valid());
        CHECK_FALSE(vp.animating());   // opens paused on the first frame

        const SDL_FRect area{0, 0, 320, 240};
        vp.render(r, font, area);      // decode + upload the first frame (no crash)

        vp.handle_key(SDLK_SPACE);     // play
        CHECK(vp.animating());

        // Drive well past the (short) clip; it must auto-pause at the end.
        for (int i = 0; i < 400 && vp.animating(); ++i) {
            vp.update(0.05);
            vp.render(r, font, area);
        }
        CHECK_FALSE(vp.animating());   // auto-paused at end of stream

        // Exercise the seek + frame-step paths (keyframe-anchored decode-forward).
        vp.handle_key(SDLK_J);         // seek -5s (clamps to 0)
        vp.handle_key(SDLK_PERIOD);    // step one frame forward
        vp.render(r, font, area);
    }

    const auto size_after = fs::file_size(tv.path, ec);
    REQUIRE(!ec);
    CHECK_EQ(size_before, size_after);   // invariant #1: no decrypted bytes to disk

    SDL_DestroyRenderer(sr);
    SDL_DestroySurface(surf);
}

#endif  // OSV_VENDORED_AV
