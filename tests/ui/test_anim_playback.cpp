#include "test_framework.h"

// AnimPlayback wires the decoder + frame-advance model + RGBA texture together.
// The pure frame-advance logic is covered by test_anim_model; this drives the
// whole glue against a real encrypted fixture, and asserts the security
// invariant that playback writes nothing to disk.
//
// The helpers and the WebP cases are deliberately OUTSIDE the OSV_VENDORED_AV
// gate: libwebp is a hard dependency, so animated WebP must play in a build
// without vendored FFmpeg. Only the GIF cases are gated.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "crypto/kdf.h"
#include "gfx/renderer.h"
#include "image/fixtures.h"
#include "ui/anim_playback.h"
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
               ("osv_gp_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
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

std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

// Headless software renderer with pixel readback, so the *upload + draw* half of
// AnimPlayback (texture format, destination rect) is covered, not just the
// decode/advance half. Matches the SoftRenderer in tests/gfx/test_renderer.cpp.
struct SoftRenderer {
    SDL_Surface*  surf = nullptr;
    SDL_Renderer* r    = nullptr;

    SoftRenderer(int w, int h)
    {
        surf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
        if (surf != nullptr) {
            r = SDL_CreateSoftwareRenderer(surf);
        }
    }
    ~SoftRenderer()
    {
        if (r != nullptr) {
            SDL_DestroyRenderer(r);
        }
        if (surf != nullptr) {
            SDL_DestroySurface(surf);
        }
    }
    // Owns two raw SDL handles that must be released exactly once (C.21).
    SoftRenderer& operator=(SoftRenderer&&) = delete;

    // Paint the whole target an unmistakable non-image colour, so an untouched
    // pixel (a letterbox band) is distinguishable from any drawn one.
    void clear_to_marker() const
    {
        SDL_SetRenderDrawColor(r, kMarkerR, kMarkerG, kMarkerB, 255);
        SDL_RenderClear(r);
    }

    struct Px { uint8_t r = 0, g = 0, b = 0, a = 0; };

    [[nodiscard]] Px at(int x, int y) const
    {
        Px p;
        SDL_ReadSurfacePixel(surf, x, y, &p.r, &p.g, &p.b, &p.a);
        return p;
    }

    static constexpr uint8_t kMarkerR = 10;
    static constexpr uint8_t kMarkerG = 20;
    static constexpr uint8_t kMarkerB = 30;
};

const vault::IndexNode* first_animated_image(const std::vector<const vault::IndexNode*>& ns)
{
    for (const vault::IndexNode* n : ns) {
        if (n != nullptr && n->is_image() && n->meta.animated) {
            return n;
        }
    }
    return nullptr;
}
}  // namespace

// --- GIF backend (needs vendored FFmpeg) --------------------------------------
#ifdef OSV_VENDORED_AV

namespace {
// Only the GIF cases read a fixture off disk; the WebP ones use the in-repo
// image fixtures, so this helper lives inside the gate to stay used in every
// build configuration.
std::vector<uint8_t> read_file(const char* file_path)
{
    std::ifstream f(file_path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
}  // namespace

TEST(anim_playback_gif_opens_an_animated_gif)
{
    auto gbytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny_anim.gif");
    REQUIRE(!gbytes.empty());

    TempVault tv("open");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("c", gbytes, "tiny_anim.gif") == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_animated_image(v.list("c"));
    REQUIRE(node != nullptr);

    ui::AnimPlayback p(v, *node);
    CHECK(p.valid());
    CHECK(p.animating());
    CHECK(!p.paused());
}

TEST(anim_playback_gif_space_toggles_pause)
{
    auto gbytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny_anim.gif");
    REQUIRE(!gbytes.empty());

    TempVault tv("pause");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("c", gbytes, "tiny_anim.gif") == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_animated_image(v.list("c"));
    REQUIRE(node != nullptr);

    ui::AnimPlayback p(v, *node);
    REQUIRE(p.valid());
    p.toggle_pause();
    CHECK(p.paused());
    CHECK(!p.animating());
    p.toggle_pause();
    CHECK(!p.paused());
    CHECK(p.animating());
}

TEST(anim_playback_gif_advances_frames_over_time)
{
    auto gbytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny_anim.gif");
    REQUIRE(!gbytes.empty());

    TempVault tv("advance");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("c", gbytes, "tiny_anim.gif") == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_animated_image(v.list("c"));
    REQUIRE(node != nullptr);

    ui::AnimPlayback p(v, *node);
    REQUIRE(p.valid());
    const size_t start = p.frames_shown();
    for (int i = 0; i < 60; ++i) p.update(0.050);   // 3 seconds of playback
    CHECK(p.frames_shown() > start);
}

TEST(anim_playback_gif_paused_does_not_advance)
{
    auto gbytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny_anim.gif");
    REQUIRE(!gbytes.empty());

    TempVault tv("paused");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("c", gbytes, "tiny_anim.gif") == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_animated_image(v.list("c"));
    REQUIRE(node != nullptr);

    ui::AnimPlayback p(v, *node);
    REQUIRE(p.valid());
    for (int i = 0; i < 10; ++i) p.update(0.050);
    p.toggle_pause();
    const size_t held = p.frames_shown();
    for (int i = 0; i < 60; ++i) p.update(0.050);
    CHECK_EQ(p.frames_shown(), held);
}

TEST(anim_playback_gif_loops_past_the_end)
{
    auto gbytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny_anim.gif");
    REQUIRE(!gbytes.empty());

    TempVault tv("loop");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("c", gbytes, "tiny_anim.gif") == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_animated_image(v.list("c"));
    REQUIRE(node != nullptr);

    ui::AnimPlayback p(v, *node);
    REQUIRE(p.valid());
    // tiny_anim.gif has 4 frames at 0.25s per frame = 1 second
    for (int i = 0; i < 200; ++i) p.update(0.050);   // 10 seconds, ~10 loops
    CHECK(p.frames_shown() > 4);                     // kept going past frame 4
    CHECK(p.animating());                            // still playing, never stalled
}

// Phase 81: the same true-colour assertion as the WebP case below, through the
// FFmpeg backend. tiny_anim.gif's first frame is yellow (252, 252, 0) at its
// centre; under the packed-format bug that pixel rendered magenta.
TEST(anim_playback_gif_renders_frames_in_true_colour)
{
    auto gbytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny_anim.gif");
    REQUIRE(!gbytes.empty());

    TempVault tv("gifcolour");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("c", gbytes, "tiny_anim.gif") == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_animated_image(v.list("c"));
    REQUIRE(node != nullptr);

    ui::AnimPlayback p(v, *node);
    REQUIRE(p.valid());

    const SoftRenderer sr(32, 32);
    REQUIRE(sr.r != nullptr);
    gfx::Renderer rr(sr.r);
    sr.clear_to_marker();
    p.render(rr, SDL_FRect{0, 0, 32, 32});      // 1:1 — no resampling
    SDL_RenderPresent(sr.r);

    const auto px = sr.at(16, 16);
    CHECK_EQ(px.r, 252);
    CHECK_EQ(px.g, 252);
    CHECK_EQ(px.b, 0);
}

#endif  // OSV_VENDORED_AV

// --- WebP backend (works in every build) --------------------------------------

TEST(anim_playback_plays_an_animated_webp)
{
    const auto wbytes = fixtures::load_anim_webp();
    REQUIRE(!wbytes.empty());

    TempVault tv("webp");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("c", wbytes, "anim.webp") == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_animated_image(v.list("c"));
    REQUIRE(node != nullptr);           // meta.animated is set at import

    ui::AnimPlayback p(v, *node);
    CHECK(p.valid());
    CHECK(p.animating());
    CHECK(!p.paused());
}

TEST(anim_playback_webp_space_toggles_pause)
{
    const auto wbytes = fixtures::load_anim_webp();
    REQUIRE(!wbytes.empty());

    TempVault tv("webppause");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("c", wbytes, "anim.webp") == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_animated_image(v.list("c"));
    REQUIRE(node != nullptr);

    ui::AnimPlayback p(v, *node);
    REQUIRE(p.valid());
    p.toggle_pause();
    CHECK(p.paused());
    CHECK(!p.animating());
    p.toggle_pause();
    CHECK(!p.paused());
    CHECK(p.animating());
}

TEST(anim_playback_webp_advances_and_loops)
{
    const auto wbytes = fixtures::load_anim_webp();
    REQUIRE(!wbytes.empty());

    TempVault tv("webpadvance");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("c", wbytes, "anim.webp") == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_animated_image(v.list("c"));
    REQUIRE(node != nullptr);

    ui::AnimPlayback p(v, *node);
    REQUIRE(p.valid());

    const size_t before = p.frames_shown();
    p.update(0.25);                     // 3 frames @ 100 ms: advances two
    CHECK(p.frames_shown() > before);

    // Past the end of a 3-frame animation it must loop, not stall: the file's
    // loop count is deliberately ignored (Phase 57 decision).
    for (int i = 0; i < 10; ++i) {
        p.update(0.1);
    }
    CHECK(p.frames_shown() > 4);
    CHECK(p.animating());
}

TEST(anim_playback_is_invalid_for_a_static_webp)
{
    const auto wbytes = fixtures::load_webp();
    REQUIRE(!wbytes.empty());

    TempVault tv("webpstatic");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("c", wbytes, "still.webp") == vault::VaultResult::Ok);

    const auto kids = v.list("c");
    REQUIRE(kids.size() == 1);

    ui::AnimPlayback p(v, *kids[0]);
    CHECK(!p.valid());                  // a single frame is not an animation
    CHECK(!p.animating());
    CHECK_EQ(p.frame_count(), static_cast<size_t>(0));
}

TEST(anim_playback_is_invalid_for_a_non_animatable_format)
{
    // A JPEG can never animate: the playback component must decline to build
    // rather than construct a decoder for it.
    const auto jbytes = fixtures::solid_jpeg(8, 8, 0x33, 0x66, 0xcc);
    REQUIRE(!jbytes.empty());

    TempVault tv("jpeg");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("c", jbytes, "photo.jpg") == vault::VaultResult::Ok);

    const auto kids = v.list("c");
    REQUIRE(kids.size() == 1);

    ui::AnimPlayback p(v, *kids[0]);
    CHECK(!p.valid());
}

// --- Rendering (Phase 81) -----------------------------------------------------
//
// Two bugs shipped unnoticed from Phase 47 to Phase 80 because no test ever
// called render(): the frames decode correctly, then the *draw* corrupts them.

// A frame's bytes are byte-order R,G,B,A (pinned for both backends at the
// decoder level). On a little-endian CPU that is SDL_PIXELFORMAT_RGBA32, i.e.
// ABGR8888 — NOT the packed SDL_PIXELFORMAT_RGBA8888, which reads the same
// bytes as A,B,G,R and turns this frame's blue into orange.
TEST(anim_playback_renders_frames_in_true_colour)
{
    const auto wbytes = fixtures::load_anim_webp();
    REQUIRE(!wbytes.empty());

    TempVault tv("webpcolour");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("c", wbytes, "anim.webp") == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_animated_image(v.list("c"));
    REQUIRE(node != nullptr);

    ui::AnimPlayback p(v, *node);
    REQUIRE(p.valid());

    const SoftRenderer sr(16, 16);
    REQUIRE(sr.r != nullptr);
    gfx::Renderer rr(sr.r);
    sr.clear_to_marker();
    p.render(rr, SDL_FRect{0, 0, 8, 8});        // 8x8 source, 1:1 — no resampling
    SDL_RenderPresent(sr.r);

    // sample_anim.webp frame 0 is solid 0x33/0x66/0xcc and lossless, so this is
    // exact (the same values test_webp_anim_decoder pins on the decoded bytes).
    const auto px = sr.at(4, 4);
    CHECK_EQ(px.r, 0x33);
    CHECK_EQ(px.g, 0x66);
    CHECK_EQ(px.b, 0xcc);
}

// The hover-preview call sites (gallery tiles, list rows, the viewer's thumbnail
// strip) hand render() the whole square cell — exactly what they hand
// ui::draw_tile_thumb / gfx::draw_thumbnail_strip, both of which aspect-fit into
// it. render() must letterbox the same way, or a non-square animation is
// visibly squashed the instant the pointer touches its tile.
TEST(anim_playback_render_letterboxes_inside_a_wider_box)
{
    const auto wbytes = fixtures::load_anim_webp();
    REQUIRE(!wbytes.empty());

    TempVault tv("webpfit");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("c", wbytes, "anim.webp") == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_animated_image(v.list("c"));
    REQUIRE(node != nullptr);

    ui::AnimPlayback p(v, *node);
    REQUIRE(p.valid());

    const SoftRenderer sr(32, 16);
    REQUIRE(sr.r != nullptr);
    gfx::Renderer rr(sr.r);
    sr.clear_to_marker();
    // 8x8 fitted into a 2:1 box -> 16x16 centred, so x in [8, 24) is the frame
    // and the 8px columns on either side are bands.
    p.render(rr, SDL_FRect{0, 0, 32, 16});
    SDL_RenderPresent(sr.r);

    const auto centre = sr.at(16, 8);
    CHECK_EQ(centre.r, 0x33);
    CHECK_EQ(centre.g, 0x66);
    CHECK_EQ(centre.b, 0xcc);

    // Bands: black backing, like the static thumbnail this replaces on hover —
    // never stretched frame content, and never the surface showing through.
    for (const auto band : {sr.at(2, 8), sr.at(29, 8)}) {
        CHECK_EQ(band.r, 0);
        CHECK_EQ(band.g, 0);
        CHECK_EQ(band.b, 0);
    }
}

// The viewer paths (render_fit / render_scroll) already pass a rect built from
// the image's own aspect ratio, so the fit must be a no-op there: a zoomed image
// still fills its destination edge to edge, with no band and no black seam.
TEST(anim_playback_render_fills_an_aspect_correct_box)
{
    const auto wbytes = fixtures::load_anim_webp();
    REQUIRE(!wbytes.empty());

    TempVault tv("webpfill");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("c", wbytes, "anim.webp") == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_animated_image(v.list("c"));
    REQUIRE(node != nullptr);

    ui::AnimPlayback p(v, *node);
    REQUIRE(p.valid());

    const SoftRenderer sr(24, 24);
    REQUIRE(sr.r != nullptr);
    gfx::Renderer rr(sr.r);
    sr.clear_to_marker();
    p.render(rr, SDL_FRect{0, 0, 24, 24});      // 8x8 at 3x zoom: same aspect
    SDL_RenderPresent(sr.r);

    for (const auto corner : {sr.at(0, 0), sr.at(23, 0), sr.at(0, 23), sr.at(23, 23)}) {
        CHECK_EQ(corner.r, 0x33);
        CHECK_EQ(corner.g, 0x66);
        CHECK_EQ(corner.b, 0xcc);
    }
}
