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
#include "media/loop_setting.h"
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

TEST(video_playback_av_sync_seek_realign_and_writes_no_disk)
{
    // Test that a video with audio (tiny_av.mp4) plays, seeks correctly,
    // re-aligns both tracks on seek, and writes zero bytes to disk.
    // Use SDL's "dummy" audio driver so the audio device opens deterministically
    // in a headless/CI environment (the master clock then becomes the audio clock).
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    auto vbytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny_av.mp4");
    REQUIRE(!vbytes.empty());

    TempVault tv("av_sync");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", vbytes, "tiny_av.mp4", 4096) == vault::VaultResult::Ok);

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
        CHECK(vp.has_audio());      // tiny_av.mp4 has audio; assertion (a)

        const SDL_FRect area{0, 0, 320, 240};
        vp.render(r, font, area);   // decode + upload the first frame
        CHECK_EQ(vp.position(), 0.0);  // initial position at 0

        vp.handle_key(SDLK_SPACE, SDL_SCANCODE_SPACE);  // play
        CHECK(vp.animating());

        // Drive several ticks while playing
        for (int i = 0; i < 10; ++i) {
            vp.update(0.05);
            vp.render(r, font, area);
        }
        // With audio open, the master clock is the audio clock (wall-clock driven),
        // so position() need not advance under synthetic update() calls. Instead
        // assert the audio path is actively feeding the device — the real "is there
        // sound?" signal, and a direct guard against the silent-playback regression.
        CHECK(vp.audio_samples_fed() > 0);

        // Seek forward by 5s (or as much as available)
        vp.handle_key(SDLK_L, SDL_SCANCODE_L);  // seek +5s (clamps to duration)
        vp.render(r, font, area);

        // Assertion (b): after seek, position should re-align to the clamped target.
        // Audio and video clocks should be synchronized. The fixture is ~1.0s, so
        // seek +5s clamps to end; assert position is within ~0.1s of end.
        const double pos_after_seek = vp.position();
        CHECK(pos_after_seek >= 0.9);  // clamped to near end (~1.0s - 0.1s tolerance)

        // Resume play to exercise audio feed path
        vp.handle_key(SDLK_SPACE, SDL_SCANCODE_SPACE);
        CHECK(vp.animating());
        for (int i = 0; i < 5; ++i) {
            vp.update(0.05);
            vp.render(r, font, area);
        }

        // Seek backward
        vp.handle_key(SDLK_J, SDL_SCANCODE_J);  // seek -5s (clamps to 0)
        vp.render(r, font, area);
        // Assertion (d): after backward seek, position should re-align to the start.
        // The fixture is ~1.0s, so seek -5s clamps to start; assert position is
        // within ~0.1s of 0.0 (both audio and video at the beginning).
        const double pos_after_backward_seek = vp.position();
        CHECK(pos_after_backward_seek <= 0.1);  // clamped to near start (~0.1s tolerance)

        // Final render to exercise any remaining decode path
        vp.render(r, font, area);
    }

    const auto size_after = fs::file_size(tv.path, ec);
    REQUIRE(!ec);
    CHECK_EQ(size_before, size_after);   // Assertion (c): no decrypted bytes to disk
                                        // (covers audio feed path)

    SDL_DestroyRenderer(sr);
    SDL_DestroySurface(surf);
}

TEST(video_playback_loop_enabled_reseeks_at_eof_instead_of_pausing)
{
    // Phase 40: with looping enabled (R key), reaching end-of-stream re-seeks
    // to 0 and keeps playing — the same do_seek(0.0) path already used by the
    // "press Space at the end" replay (Phase 16), reached automatically from
    // advance()'s EOF branch instead of a user keypress. Uses a fixture with
    // no audio track so the transport clock is the deterministic model clock,
    // not the audio clock.
    media::set_saved_loop_enabled(false);   // known starting state (shared global)

    auto vbytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!vbytes.empty());

    TempVault tv("loop_eof");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", vbytes, "tiny.mp4", 4096) == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_video(v.list("c"));
    REQUIRE(node != nullptr);

    SDL_Surface* surf = SDL_CreateSurface(320, 240, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(surf != nullptr);
    SDL_Renderer* sr = SDL_CreateSoftwareRenderer(surf);
    REQUIRE(sr != nullptr);
    gfx::Renderer r(sr);
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(OSV_DEFAULT_FONT, 18.0f));

    ui::VideoPlayback vp(v, *node);
    REQUIRE(vp.valid());
    CHECK(!vp.has_audio());   // tiny.mp4 has no audio track — model clock drives it

    const SDL_FRect area{0, 0, 320, 240};
    vp.render(r, font, area);   // decode + upload frame 0

    vp.handle_key(SDLK_R, SDL_SCANCODE_R);          // enable loop
    vp.handle_key(SDLK_SPACE, SDL_SCANCODE_SPACE);  // play
    REQUIRE(vp.animating());

    // Drive well past the fixture's duration (~1s at 10fps); with loop
    // enabled, playback must still be animating (never auto-paused at EOF)
    // and position() must have wrapped back down near the start.
    for (int i = 0; i < 30; ++i) {
        vp.update(0.1);
        vp.render(r, font, area);
    }
    CHECK(vp.animating());        // still playing, not auto-paused at EOF
    CHECK(vp.position() < 1.0);   // wrapped back near the start, not stuck at the end

    media::set_saved_loop_enabled(false);   // restore the default (shared global)

    SDL_DestroyRenderer(sr);
    SDL_DestroySurface(surf);
}

TEST(video_playback_loop_disabled_pauses_at_eof)
{
    // Control case: with loop off (the default), reaching EOF still
    // auto-pauses — Phase 16's existing behavior, confirmed unchanged by the
    // Phase 40 loop branch added to advance()'s EOF check.
    media::set_saved_loop_enabled(false);   // known starting state (shared global)

    auto vbytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!vbytes.empty());

    TempVault tv("no_loop_eof");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", vbytes, "tiny.mp4", 4096) == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_video(v.list("c"));
    REQUIRE(node != nullptr);

    SDL_Surface* surf = SDL_CreateSurface(320, 240, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(surf != nullptr);
    SDL_Renderer* sr = SDL_CreateSoftwareRenderer(surf);
    REQUIRE(sr != nullptr);
    gfx::Renderer r(sr);
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(OSV_DEFAULT_FONT, 18.0f));

    ui::VideoPlayback vp(v, *node);
    REQUIRE(vp.valid());

    const SDL_FRect area{0, 0, 320, 240};
    vp.render(r, font, area);
    vp.handle_key(SDLK_SPACE, SDL_SCANCODE_SPACE);  // play (loop stays off)
    REQUIRE(vp.animating());

    for (int i = 0; i < 30; ++i) {
        vp.update(0.1);
        vp.render(r, font, area);
    }
    CHECK(!vp.animating());   // auto-paused at EOF, unchanged Phase 16 behavior

    SDL_DestroyRenderer(sr);
    SDL_DestroySurface(surf);
}

TEST(video_playback_opens_audio_device_for_clip_with_sound)
{
    // Regression: the app inits only SDL_INIT_VIDEO, so VideoPlayback must bring
    // up the audio subsystem itself before opening a device — otherwise a clip
    // with sound plays silently. Use SDL's "dummy" audio driver so the device
    // opens deterministically in a headless/CI environment (no real hardware).
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    auto vbytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny_av.mp4");
    REQUIRE(!vbytes.empty());

    TempVault tv("audio_dev");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", vbytes, "tiny_av.mp4", 4096) == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_video(v.list("c"));
    REQUIRE(node != nullptr);

    ui::VideoPlayback vp(v, *node);
    REQUIRE(vp.valid());
    REQUIRE(vp.has_audio());          // the clip carries an audio track (decoder-level)
    // The actual regression: the audio output device must open. Before the fix,
    // SDL_OpenAudioDeviceStream fails with "Audio subsystem is not initialized".
    CHECK(vp.audio_active());
    CHECK(SDL_WasInit(SDL_INIT_AUDIO) != 0);
}

TEST(video_playback_volume_bar_is_mouse_draggable_and_speaker_mutes)
{
    // The volume control must be usable with the mouse (drag the bar; click the
    // speaker to mute) — keyboard-only [/]/M was undiscoverable. Dummy driver so
    // the device opens headlessly.
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    auto vbytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny_av.mp4");
    REQUIRE(!vbytes.empty());

    TempVault tv("vol");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", vbytes, "tiny_av.mp4", 4096) == vault::VaultResult::Ok);
    const vault::IndexNode* node = first_video(v.list("c"));
    REQUIRE(node != nullptr);

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
        REQUIRE(vp.audio_active());          // device open (else there is no volume bar)

        const SDL_FRect area{0, 0, 320, 240};
        vp.render(r, font, area);            // computes the volume-bar geometry

        const SDL_FRect bar = vp.debug_vol_bar();
        REQUIRE(bar.w > 0.0f);               // a real, hittable bar exists
        const float my = bar.y + bar.h * 0.5f;

        // Drag to the far left -> ~0 gain; far right -> ~1.0; middle -> ~0.5.
        vp.handle_mouse_down(bar.x, my);
        CHECK(vp.audio_gain() < 0.1f);
        vp.handle_mouse_down(bar.x + bar.w, my);
        CHECK(vp.audio_gain() > 0.9f);
        vp.handle_mouse_motion(bar.x + bar.w * 0.5f, my, true);   // drag to middle
        CHECK(vp.audio_gain() > 0.4f);
        CHECK(vp.audio_gain() < 0.6f);
        vp.handle_mouse_up();

        // Clicking the speaker icon toggles mute (gain drops to 0, then restores).
        const float gain_before_mute = vp.audio_gain();
        CHECK(gain_before_mute > 0.0f);
        vp.handle_key(SDLK_M, SDL_SCANCODE_M);
        CHECK_EQ(vp.audio_gain(), 0.0f);
        vp.handle_key(SDLK_M, SDL_SCANCODE_M);
        CHECK(vp.audio_gain() > 0.0f);

        // Volume `[`/`]` bind to the physical SCANCODE (Phase 25), so they work on a
        // non-US layout where those glyphs differ. Simulate a German layout: the
        // physical `[`/`]` keys produce non-bracket keycodes, yet volume still moves.
        const float mid = vp.audio_gain();
        vp.handle_key(SDLK_UNKNOWN, SDL_SCANCODE_LEFTBRACKET);   // volume down
        CHECK(vp.audio_gain() < mid);
        const float lowered = vp.audio_gain();
        vp.handle_key(SDLK_UNKNOWN, SDL_SCANCODE_RIGHTBRACKET);  // volume up
        CHECK(vp.audio_gain() > lowered);
    }

    SDL_DestroyRenderer(sr);
    SDL_DestroySurface(surf);
}

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

        vp.handle_key(SDLK_SPACE, SDL_SCANCODE_SPACE);     // play
        CHECK(vp.animating());

        // Drive well past the (short) clip; it must auto-pause at the end.
        for (int i = 0; i < 400 && vp.animating(); ++i) {
            vp.update(0.05);
            vp.render(r, font, area);
        }
        CHECK_FALSE(vp.animating());   // auto-paused at end of stream

        // Exercise the seek + frame-step paths (keyframe-anchored decode-forward).
        vp.handle_key(SDLK_J, SDL_SCANCODE_J);         // seek -5s (clamps to 0)
        vp.handle_key(SDLK_PERIOD, SDL_SCANCODE_PERIOD);    // step one frame forward
        vp.render(r, font, area);
    }

    const auto size_after = fs::file_size(tv.path, ec);
    REQUIRE(!ec);
    CHECK_EQ(size_before, size_after);   // invariant #1: no decrypted bytes to disk

    SDL_DestroyRenderer(sr);
    SDL_DestroySurface(surf);
}

TEST(video_playback_seek_moves_position_and_stays_paused)
{
    // Phase 39 Part 2: seek() restores a session resume bookmark right after
    // construction, without starting playback (the resumed clip stays paused
    // until the user presses play).
    auto vbytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!vbytes.empty());

    TempVault tv("seek_api");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", vbytes, "tiny.mp4", 4096) == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_video(v.list("c"));
    REQUIRE(node != nullptr);

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
        CHECK_FALSE(vp.animating());   // opens paused

        vp.render(r, font, {0, 0, 320, 240});   // decode + upload frame 0
        CHECK_EQ(vp.position(), 0.0);

        vp.seek(0.3);
        vp.render(r, font, {0, 0, 320, 240});
        CHECK(vp.position() >= 0.2);   // moved close to the requested target
        CHECK_FALSE(vp.animating());   // still paused after the seek
    }

    SDL_DestroyRenderer(sr);
    SDL_DestroySurface(surf);
}

TEST(video_playback_decodes_av1_and_writes_no_disk)
{
    // Phase 40 hardening (a): AV1 .webm plays through the full VideoPlayback
    // pipeline (decoder + av_sync + audio), not just raw decode order —
    // confirms av_sync::decide's Present/Hold/Drop classification holds for
    // the new codec's frame timestamps, mirroring the seek-realign test above.
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    auto vbytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny_av1.webm");
    REQUIRE(!vbytes.empty());

    TempVault tv("av1_playback");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", vbytes, "clip.webm", 4096) == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_video(v.list("c"));
    REQUIRE(node != nullptr);

    std::error_code ec;
    const auto size_before = fs::file_size(tv.path, ec);
    REQUIRE(!ec);

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
        CHECK(vp.has_audio());

        const SDL_FRect area{0, 0, 320, 240};
        vp.render(r, font, area);   // decode + upload frame 0
        vp.handle_key(SDLK_SPACE, SDL_SCANCODE_SPACE);
        CHECK(vp.animating());

        for (int i = 0; i < 10; ++i) {
            vp.update(0.05);
            vp.render(r, font, area);
        }
        CHECK(vp.audio_samples_fed() > 0);   // audio actively flowing, in sync with video
    }

    const auto size_after = fs::file_size(tv.path, ec);
    REQUIRE(!ec);
    CHECK_EQ(size_before, size_after);   // no decrypted bytes to disk

    SDL_DestroyRenderer(sr);
    SDL_DestroySurface(surf);
}

TEST(video_playback_loop_reseek_realigns_audio_like_manual_seek)
{
    // Phase 40 hardening (b): the loop-boundary reseek reuses do_seek() — the
    // exact path the seek-realign test at the top of this file already
    // verifies flushes and re-aligns both tracks for a keypress-triggered
    // seek. Confirm the same invariant holds when do_seek() fires
    // automatically from EOF (loop) instead.
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    media::set_saved_loop_enabled(false);   // known starting state (shared global)

    auto vbytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny_av.mp4");
    REQUIRE(!vbytes.empty());

    TempVault tv("loop_audio_realign");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", vbytes, "tiny_av.mp4", 4096) == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_video(v.list("c"));
    REQUIRE(node != nullptr);

    SDL_Surface* surf = SDL_CreateSurface(320, 240, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(surf != nullptr);
    SDL_Renderer* sr = SDL_CreateSoftwareRenderer(surf);
    REQUIRE(sr != nullptr);
    gfx::Renderer r(sr);
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(OSV_DEFAULT_FONT, 18.0f));

    ui::VideoPlayback vp(v, *node);
    REQUIRE(vp.valid());
    REQUIRE(vp.has_audio());

    const SDL_FRect area{0, 0, 320, 240};
    vp.render(r, font, area);
    vp.handle_key(SDLK_R, SDL_SCANCODE_R);           // enable loop
    vp.handle_key(SDLK_SPACE, SDL_SCANCODE_SPACE);   // play
    REQUIRE(vp.animating());

    // Drive well past the ~1s fixture's duration so the loop boundary fires.
    for (int i = 0; i < 40; ++i) {
        vp.update(0.1);
        vp.render(r, font, area);
    }

    // Same invariants the manual-seek test above checks: after wrapping,
    // position re-aligns near the start (both clocks reset by do_seek),
    // playback keeps running, and audio keeps flowing across the boundary.
    CHECK(vp.animating());
    CHECK(vp.position() < 1.0);
    CHECK(vp.audio_samples_fed() > 0);

    media::set_saved_loop_enabled(false);   // restore the default (shared global)

    SDL_DestroyRenderer(sr);
    SDL_DestroySurface(surf);
}

#endif  // OSV_VENDORED_AV
