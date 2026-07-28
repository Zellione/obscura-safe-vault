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
