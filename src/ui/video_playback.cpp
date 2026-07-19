#include "ui/video_playback.h"

#include <algorithm>
#include <string>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "ui/keybindings.h"    // volume_dir (layout-robust volume keys)
#include "ui/playback_model.h"
#include "ui/viewer_model.h"   // fit_zoom

#ifdef OSV_VENDORED_AV
#include <memory>
#include <optional>
#include <print>

#include "gfx/yuv_texture.h"
#include "image/decode_worker.h"
#include "media/av_sync.h"
#include "media/chunk_avio.h"
#include "media/loop_setting.h"
#include "media/video_decode_worker.h"
#include "media/video_decoder.h"
#include "media/video_source.h"
#include "media/volume_setting.h"
#include "vault/index.h"
#include "vault/vault.h"
#endif

namespace ui {

namespace {
constexpr float  CONTROL_H        = 56.0f;       // transport strip height
constexpr float  PAD              = 16.0f;
constexpr float  TRACK_H          = 10.0f;
constexpr float  KNOB_R           = 8.0f;
constexpr double DEFAULT_FRAME_DT = 1.0 / 30.0;  // backward frame-step fallback
constexpr size_t PREFETCH_DEPTH   = 2;           // packets kept queued ahead of the video decode worker
constexpr size_t MAX_STEADY_IN_FLIGHT = 16;      // ceiling on in-flight packets outside an active seek —
                                                  // covers codec startup/B-frame reordering delay without
                                                  // letting the backlog grow without bound over a whole clip
}  // namespace

#ifdef OSV_VENDORED_AV

// Full implementation: a ChunkAvio over the vault's encrypted chunks feeds a
// VideoDecoder; decoded YUV frames upload to a streaming texture. The pure
// PlaybackModel owns the transport clock. No bytes touch disk (invariant #1).
struct VideoPlayback::Impl {
    media::VideoDecoder                          decoder_;
    std::unique_ptr<media::VideoDecodeWorker>    video_worker_;
    uint64_t                                     generation_    = 0;
    size_t                                       in_flight_     = 0;      // packets submitted, Result not yet consumed
    bool                                          demux_eof_     = false;  // demuxer hit EOF for the current generation
    bool skip_pending_ = false;  // a seek's decode-forward-to-target skip may still be discarding
                                 // frames without publishing a Result
    std::unique_ptr<media::ChunkAvio> avio_;
    gfx::YuvTexture                   yuv_;
    PlaybackModel                     model_{0.0};
    bool                              valid_ = false;

    // State of the frame most recently decoded and awaiting presentation,
    // and how it relates to what was last shown — produced by
    // decode_into_pending()/try_advance_pending(), shown by
    // present_pending(), driven by advance() against the playback clock.
    struct FrameState {
        std::optional<media::DecodedFrame> pending;  // next decoded frame (planes valid)
        std::vector<uint8_t> pending_storage;  // backs pending->planes (owned copy from the worker)
        double pending_pts = 0.0;
        double shown_pts = -1.0;             // last presented frame's pts
        double frame_dt = DEFAULT_FRAME_DT;  // estimated, for backward step
        bool need_present = false;           // force-show pending (open / seek)
        bool eof = false;
    };
    FrameState frame_;

    bool      scrubbing_    = false;
    bool      loop_         = false;                     // seeded from media::saved_loop_enabled()
    SDL_FRect loop_icon_{};                               // last-rendered loop icon rect
    SDL_FRect track_{};                                  // last-rendered seek-bar track rect

    // Audio playback state.
    struct AudioState {
        SDL_AudioStream* stream = nullptr;
        uint64_t samples_fed = 0;
        double seek_base = 0.0;
        bool subsystem_owned = false;  // we brought up SDL_INIT_AUDIO; quit it in dtor
    };
    AudioState audio_;

    // Volume-control widget state (the draggable bar + speaker/mute icon).
    struct VolumeUi {
        float     level     = 1.0f;   // 0..1
        bool      muted     = false;
        SDL_FRect bar{};              // last-rendered volume-bar rect (mouse target)
        SDL_FRect icon{};             // last-rendered speaker/mute icon rect
        bool      scrubbing = false;  // dragging the bar
    };
    VolumeUi vol_;

    Impl(const vault::Vault& vault, const vault::IndexNode& node,
         std::chrono::milliseconds test_only_decode_delay)
    {
        vol_.level = media::saved_volume();   // remembered across clips + restarts (Phase 25)
        loop_      = media::saved_loop_enabled();   // remembered for the session (Phase 40)
        auto src = media::VideoSource::open(vault, node);
        avio_ = std::make_unique<media::ChunkAvio>(std::move(src));
        if (!avio_->valid()) {
            std::println(stderr, "[VideoPlayback] AVIO init failed");
            return;
        }
        if (!decoder_.open(avio_->ctx())) {
            std::println(stderr, "[VideoPlayback] decoder open failed");
            return;
        }
        valid_ = true;
        video_worker_ = std::make_unique<media::VideoDecodeWorker>(
            *decoder_.video_codecpar(), decoder_.video_time_base(), image::decode_wake_event(),
            test_only_decode_delay);
        model_ = PlaybackModel(static_cast<double>(decoder_.duration_us()) / 1'000'000.0);

        // Open audio device if available. The app only inits SDL_INIT_VIDEO, so
        // the audio subsystem must be brought up here before any device can open
        // (SDL_OpenAudioDeviceStream fails with "Audio subsystem is not
        // initialized" otherwise). Audio is optional: tolerate init/open failure
        // (no audio hardware) and keep playing video. SDL_InitSubSystem is
        // ref-counted; the matching SDL_QuitSubSystem runs in the destructor.
        if (decoder_.has_audio()) {
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
                std::println(stderr, "[VideoPlayback] audio subsystem init failed: {}",
                             SDL_GetError());
            } else {
                audio_.subsystem_owned = true;
                SDL_AudioSpec audio_spec{};
                audio_spec.format   = SDL_AUDIO_F32;
                audio_spec.channels = decoder_.audio_info().channels;
                audio_spec.freq     = decoder_.audio_info().sample_rate;
                audio_.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                          &audio_spec, nullptr, nullptr);
                if (!audio_.stream) {
                    std::println(stderr, "[VideoPlayback] audio open failed: {}", SDL_GetError());
                } else {
                    SDL_SetAudioStreamGain(audio_.stream,
                                           media::effective_gain(vol_.level, vol_.muted));
                }
            }
        }

        decode_into_pending();       // first frame ready; uploaded lazily on first render
        frame_.need_present = true;  // show frame 0 immediately, paused
    }

    ~Impl()
    {
        if (audio_.stream) SDL_DestroyAudioStream(audio_.stream);  // release the device first
        if (audio_.subsystem_owned) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    // Demuxes and submits one packet to the worker under the current
    // generation, bumping in_flight_ and marking demux_eof_ once the
    // demuxer is exhausted (pkt == nullptr, the end-of-stream marker).
    void feed_one_packet()
    {
        AVPacket* pkt = decoder_.demux_next_video_packet();
        video_worker_->submit(pkt, generation_);
        ++in_flight_;
        if (!pkt) demux_eof_ = true;
    }

    void prefetch_upto(size_t depth)
    {
        while (in_flight_ < depth && !demux_eof_)
            feed_one_packet();
    }

    // Consumes a Result already confirmed to match the current generation:
    // updates in_flight_/skip_pending_ bookkeeping, then either marks eof_
    // or publishes the new pending frame and its measured frame_dt.
    void consume_result(media::VideoDecodeWorker::Result& r)
    {
        --in_flight_;
        skip_pending_ = false;  // the worker has published a real result for this
                                // generation, so any seek-skip window has closed —
                                // subsequent timeouts mean "still decoding", not
                                // "silently discarded toward an old target".
        if (r.eof) {
            frame_.eof = true;
            frame_.pending.reset();
            return;
        }
        frame_.pending_storage = std::move(r.storage);
        frame_.pending = r.frame;
        const double p = frame_.pending->pts_seconds;
        if (frame_.shown_pts >= 0.0 && p > frame_.shown_pts)
            frame_.frame_dt = std::max(1e-3, p - frame_.shown_pts);
        frame_.pending_pts = p;
    }

    // Tops up the worker's queue to PREFETCH_DEPTH packets ahead (so the
    // worker has a head start decoding while the render thread does other
    // work), then blocks for the next in-generation result. Mirrors
    // decoder_.next_frame()'s old blocking contract — the caller can rely on
    // pending_ being up to date when this returns — but the wait now
    // overlaps with the worker's own progress instead of being the decode
    // itself.
    //
    // While a seek target is pending (skip_pending_), the worker's own
    // decode-forward logic silently discards frames whose pts lands before
    // the target — no Result is ever published for those, so in_flight_
    // never reflects them. A fixed PREFETCH_DEPTH alone can't get past
    // this: once PREFETCH_DEPTH packets are in flight and all of them get
    // silently discarded, the render thread would otherwise just wait
    // forever for a Result that's never coming, having no way to know the
    // worker needs more input rather than more time. So while skip_pending_
    // is set, feed one more packet on every wait_result() timeout before
    // retrying, uncapped — a seek's decode-forward gap is a one-time,
    // GOP-bounded cost, not a per-frame recurring one, and the existing
    // av_sync seek-realign test expects a seek to fully resolve within a
    // single decode_into_pending() call.
    //
    // Outside a pending seek, every submitted packet is still guaranteed to
    // eventually publish exactly one Result (frame or eof), so a timeout
    // usually just means "still decoding", not "lost" — BUT some codecs
    // need to see several packets before their first frame is emitted at
    // all (B-frame reordering / decoder lookahead, routine and unrelated to
    // seeking), so decode_into_pending() must still be willing to feed a
    // little more input on a timeout even with no seek pending, or the
    // very first frame(s) of a stream could stall forever on a fixed
    // PREFETCH_DEPTH. The fix is a bounded ceiling
    // (MAX_STEADY_IN_FLIGHT), not an outright ban: retry-feed while under
    // it (covers startup/reordering delay), then just keep waiting once at
    // it. Unconditionally feeding more on every timeout with no ceiling (an
    // earlier version of this function did that) let the render thread
    // out-submit the worker without bound whenever a single frame's decode
    // legitimately took longer than wait_result()'s short timeout (routine
    // for a slower codec, e.g. software AV1) — in_flight_ (and the
    // worker's real backlog) would only ratchet upward, call after call,
    // for the rest of playback, since each call only ever consumes one
    // Result no matter how many extra packets its own retries had
    // submitted. That is what made playback (and eventually the whole app,
    // contending for the same CPU) get progressively slower the longer a
    // clip played — this bounded ceiling fixes that while still leaving
    // enough slack for legitimate startup/reordering delay.
    void decode_into_pending()
    {
        prefetch_upto(PREFETCH_DEPTH);

        for (;;) {
            auto r = video_worker_->wait_result();
            if (!r) {
                if (const bool may_feed_more = skip_pending_ || in_flight_ < MAX_STEADY_IN_FLIGHT;
                    may_feed_more && !demux_eof_) {
                    feed_one_packet();
                    continue;
                }
                if (in_flight_ > 0) {
                    // Nothing left to feed (at the ceiling, or the demuxer
                    // is exhausted) but jobs already submitted are still
                    // outstanding — every one of them is guaranteed to
                    // eventually publish a Result (frame or eof), so keep
                    // waiting rather than give up. Giving up here
                    // unconditionally whenever demux_eof_ was true (as an
                    // earlier version of this function did, inherited
                    // unchanged from how Phase 41 originally shipped) could
                    // abandon pending_ update entirely: a short clip's
                    // remaining packets can all get demuxed and submitted
                    // well before the worker finishes decoding even the
                    // first one of them if decode is slow enough (e.g. real
                    // software AV1) — reaching demux_eof_ with several jobs
                    // still queued, at which point the old code returned
                    // immediately without ever having consumed a single
                    // Result, leaving pending_ stuck instead of eventually
                    // populated. The existing tiny (near-instant-decode)
                    // fixtures never hit this window in practice, which is
                    // why it went unnoticed until a genuinely slow codec.
                    continue;
                }
                // Truly nothing outstanding anywhere (in_flight_ == 0) and
                // nothing left to feed — give up for this render pass; the
                // caller (present_pending()/advance()) retries next tick.
                return;
            }
            if (r->generation != generation_) {
                // Stale result from a superseded seek/generation; discard it without
                // decrementing in_flight_ (stale results don't correspond to packets
                // we submitted in the current generation).
                continue;
            }
            consume_result(*r);
            return;
        }
    }

    // Bounded-wait counterpart to decode_into_pending(), used by steady
    // (non-seek) playback instead of it. App::run()'s main loop only pumps
    // SDL events BEFORE calling render() — any call inside render() that
    // blocks for as long as one frame's decode takes (100s of ms for a
    // slow software codec, e.g. real AV1) blocks input handling for that
    // same duration, on every single frame. That defeated the entire
    // point of moving decode off-thread: the render thread would still
    // effectively stall per frame, just waiting on a condvar instead of
    // doing the decode itself.
    //
    // Tries once (a single VideoDecodeWorker::wait_result() call, capped
    // by its own short ~20ms internal timeout — no retry loop) to advance
    // pending_ to the next frame. A zero-wait poll (take_result()) was
    // tried first but doesn't work: it never yields the calling thread, so
    // under back-to-back render() calls the OS scheduler can go a long
    // time without ever handing the worker thread a timeslice to actually
    // decode anything — playback would stall not because anything is
    // stuck, but because the worker was simply never scheduled. A single
    // bounded wait per call caps this call's own added cost at ~20ms
    // (imperceptible for input responsiveness, and nowhere near the
    // unbounded blocking decode_into_pending() used to cost) while
    // guaranteeing the worker actually gets to run.
    //
    // If nothing is ready by the time the wait times out, pending_ ends up
    // nullopt (NOT left holding its old value — advance()'s
    // while(pending_) loop relies on nullopt to mean "nothing to do right
    // now", and leaving a stale non-null value in place would spin that
    // loop forever re-processing the same already-handled frame) and this
    // returns — the render loop just tries again on the next tick, which
    // happens right away while animating() is true. do_seek() and the
    // constructor's initial frame keep using the original blocking
    // decode_into_pending(): a seek is a one-time, bounded user action
    // where resolving within a single call is the expected (and already
    // tested) contract, not a per-frame recurring cost.
    void try_advance_pending()
    {
        frame_.pending.reset();
        prefetch_upto(PREFETCH_DEPTH);

        auto r = video_worker_->wait_result();
        for (;;) {
            if (!r) {
                // Nothing ready within the bounded wait. Some codecs need
                // several packets queued before their very first frame is
                // emitted at all (B-frame reordering/decoder lookahead,
                // unrelated to seeking) — PREFETCH_DEPTH alone can
                // permanently starve decode if that's not enough, since
                // in_flight_ never rises above it without this. Feed at
                // most ONE extra packet per call (not a retry loop) so
                // this call's own cost stays capped at a single
                // wait_result(); if still more is needed, later ticks keep
                // feeding one at a time until decode catches up.
                if (in_flight_ < MAX_STEADY_IN_FLIGHT && !demux_eof_) feed_one_packet();
                return;   // caller retries next tick
            }
            if (r->generation != generation_) {
                r = video_worker_->take_result();   // drain further stale results, non-blocking
                continue;
            }
            consume_result(*r);
            return;
        }
    }

    void pump_audio()
    {
        if (!audio_.stream) return;
        const int ch = decoder_.audio_info().channels;
        const int sr = decoder_.audio_info().sample_rate;
        const int target = sr * ch * (int)sizeof(float) / 5;   // ~200ms buffered
        while (SDL_GetAudioStreamQueued(audio_.stream) < target) {
            auto a = decoder_.next_audio_frame();
            if (!a) break;
            SDL_PutAudioStreamData(audio_.stream, a->samples.data(),
                                   (int)(a->samples.size() * sizeof(float)));
            audio_.samples_fed += a->samples.size() / (ch > 0 ? ch : 1);
        }
    }

    double clock() const
    {
        if (!audio_.stream) return model_.position();
        const int ch = decoder_.audio_info().channels;
        const int sr = decoder_.audio_info().sample_rate;
        uint64_t queued_samples =
            (uint64_t)SDL_GetAudioStreamQueued(audio_.stream) / (sizeof(float) * (ch > 0 ? ch : 1));
        uint64_t consumed =
            audio_.samples_fed > queued_samples ? audio_.samples_fed - queued_samples : 0;
        return media::audio_clock(audio_.seek_base, consumed, sr);
    }

    // Upload pending_ to the GPU (durable copy) then try (bounded-wait,
    // ~20ms max) to line up the next frame.
    void present_pending(SDL_Renderer* r)
    {
        if (!frame_.pending) return;
        if (yuv_.ensure(r, frame_.pending->width, frame_.pending->height, frame_.pending->pix_fmt))
            (void)yuv_.update(*frame_.pending);
        frame_.shown_pts = frame_.pending_pts;
        frame_.need_present = false;
        try_advance_pending();
    }

    // Decode + upload frames using audio clock (if available) or model clock fallback.
    void advance(SDL_Renderer* r)
    {
        pump_audio();  // Ensure audio buffer is topped up

        const double c = clock();
        // Sync model position toward audio clock when audio is active
        if (audio_.stream && model_.playing()) {
            model_.seek_to(c);
        }

        // A prior tick's non-blocking try_advance_pending() may have found
        // nothing ready yet, leaving pending null without eof having been
        // reached — the while loop below only polls when pending is
        // already truthy, so without this, that transient "not ready yet"
        // would never get retried and playback would stall permanently.
        if (!frame_.pending && !frame_.eof) {
            try_advance_pending();
        }

        // Drive video frames against the clock using av_sync
        while (frame_.pending) {
            if (frame_.need_present) {
                present_pending(r);
                frame_.need_present = false;
            } else {
                media::FrameAction act = media::decide(c, frame_.pending_pts);
                if (act == media::FrameAction::Hold) {
                    break;
                } else if (act == media::FrameAction::Drop) {
                    try_advance_pending();
                } else {  // Present
                    present_pending(r);
                }
            }
        }

        if (frame_.eof && !frame_.pending && model_.playing()) {
            if (loop_) {
                do_seek(0.0);   // loop: re-seek to start, keep playing (same path
                                 // as the "press Space at the end" replay)
            } else {
                model_.set_playing(false);   // pause at end of stream
            }
        }
    }

    void do_seek(double t)
    {
        const double tt = clamp_time(t, model_.duration());
        if (!decoder_.seek_demux_only(tt)) return;
        ++generation_;
        video_worker_->begin_seek(tt);
        in_flight_  = 0;       // begin_seek() drops every queued job; none of them will ever
                                // publish a Result, so the render thread's own count must
                                // reset too, or decode_into_pending() would under-prefetch
                                // forever after every seek.
        demux_eof_    = false;   // the seek moves the demuxer away from end-of-stream
        skip_pending_ = true;    // until the worker publishes its first post-seek Result,
                                  // frames below the target may be silently discarded
        if (audio_.stream) SDL_ClearAudioStream(audio_.stream);
        audio_.samples_fed = 0;
        audio_.seek_base = tt;
        frame_.pending.reset();
        frame_.shown_pts = -1.0;
        frame_.eof = false;
        decode_into_pending();
        model_.seek_to(frame_.pending ? frame_.pending_pts : tt);
        frame_.need_present = true;
    }

    void apply_pause_resume()
    {
        if (audio_.stream) {
            if (model_.playing())
                SDL_ResumeAudioStreamDevice(audio_.stream);
            else
                SDL_PauseAudioStreamDevice(audio_.stream);
        }
    }

    // Push the current volume/mute level to the audio stream (no-op when muted or
    // when the clip has no audio track).
    void apply_gain()
    {
        if (audio_.stream) {
            SDL_SetAudioStreamGain(audio_.stream, media::effective_gain(vol_.level, vol_.muted));
        }
    }

    void adjust_volume(float delta)
    {
        vol_.level = media::clamp_volume(vol_.level + delta);
        media::set_saved_volume(vol_.level);   // remember it (Phase 25)
        apply_gain();
    }

    void key(SDL_Keycode k, SDL_Scancode sc)
    {
        // Volume: `[`/`]` (incl. German QWERTZ AltGr+8/9, resolved to the produced
        // character), the `-`/`+`/`=` glyph keys, or the physical bracket positions
        // — all layout-robust (Phase 25 fix). Resolve the character the layout +
        // held modifiers produce, exactly like the `/` search shortcut.
        constexpr float kVolStep = 0.05f;
        using enum VolumeDir;
        switch (const SDL_Keycode produced = SDL_GetKeyFromScancode(sc, SDL_GetModState(), false);
                volume_dir(produced, sc)) {
            case Down: adjust_volume(-kVolStep); return;
            case Up:   adjust_volume(kVolStep);  return;
            case None: break;
        }
        switch (k) {
            case SDLK_SPACE:
                if (model_.at_end() && !model_.playing()) {
                    do_seek(0.0);
                    model_.set_playing(true);
                } else {
                    model_.toggle();
                }
                apply_pause_resume();
                break;
            case SDLK_COMMA:   // step back one frame (paused)
                model_.set_playing(false);
                apply_pause_resume();
                do_seek(model_.position() - frame_.frame_dt);
                break;
            case SDLK_PERIOD:  // step forward one frame (paused)
                model_.set_playing(false);
                apply_pause_resume();
                if (frame_.pending) {
                    model_.seek_to(frame_.pending_pts);
                    frame_.need_present = true;
                }
                break;
            case SDLK_J: do_seek(model_.position() - PLAYBACK_SEEK_STEP); break;
            case SDLK_L: do_seek(model_.position() + PLAYBACK_SEEK_STEP); break;
            case SDLK_M:
                vol_.muted = !vol_.muted;
                apply_gain();
                break;
            case SDLK_R:
                loop_ = !loop_;
                media::set_saved_loop_enabled(loop_);
                break;
            default: break;
        }
    }

    [[nodiscard]] bool on_track(float mx, float my) const
    {
        // Bigger invisible grab zone than the visible bar (Phase 45 Part 5) —
        // a thin, tasteful track with an easy-to-hit click/drag target.
        constexpr float grab_y = 18.0f;
        return mx >= track_.x - KNOB_R && mx <= track_.x + track_.w + KNOB_R &&
               my >= track_.y - grab_y && my <= track_.y + track_.h + grab_y;
    }

    [[nodiscard]] static bool in_rect(const SDL_FRect& rc, float mx, float my, float pad)
    {
        return rc.w > 0.0f && mx >= rc.x - pad && mx <= rc.x + rc.w + pad &&
               my >= rc.y - pad && my <= rc.y + rc.h + pad;
    }

    void set_volume_from_x(float mx)
    {
        if (vol_.bar.w <= 0.0f) return;
        vol_.level = media::clamp_volume((mx - vol_.bar.x) / vol_.bar.w);
        media::set_saved_volume(vol_.level);       // remember it (Phase 25)
        vol_.muted  = false;                       // grabbing the volume bar unmutes
        apply_gain();
    }

    void mouse_down(float mx, float my)
    {
        if (audio_.stream) {
            if (in_rect(vol_.icon, mx, my, 4.0f)) {     // click the speaker to (un)mute
                vol_.muted = !vol_.muted;
                apply_gain();
                return;
            }
            if (in_rect(vol_.bar, mx, my, 8.0f)) {    // click/drag the volume bar
                vol_.scrubbing = true;
                set_volume_from_x(mx);
                return;
            }
        }
        if (on_track(mx, my)) {
            scrubbing_ = true;
            do_seek(bar_x_to_time(mx, model_.duration(), track_.x, track_.w));
        }
    }

    void mouse_motion(float mx, float /*my*/, bool left_down)
    {
        if (vol_.scrubbing && left_down)
            set_volume_from_x(mx);
        else if (scrubbing_ && left_down)
            do_seek(bar_x_to_time(mx, model_.duration(), track_.x, track_.w));
    }

    void mouse_up() { scrubbing_ = false; vol_.scrubbing = false; }

    void render(gfx::Renderer& rr, gfx::FontAtlas& font, const SDL_FRect& area)
    {
        SDL_Renderer* r = rr.sdl();
        advance(r);

        const SDL_FRect ctrl{area.x, area.y + area.h - CONTROL_H, area.w, CONTROL_H};
        const SDL_FRect vid{area.x, area.y, area.w, std::max(0.0f, area.h - CONTROL_H)};

        rr.draw_rect(vid, gfx::theme::IMG_BG);
        if (SDL_Texture* tex = yuv_.texture()) {
            float tw = 0.0f;
            float th = 0.0f;
            SDL_GetTextureSize(tex, &tw, &th);
            const float s = fit_zoom(tw, th, vid.w, vid.h);
            const float w = tw * s;
            const float h = th * s;
            rr.draw_image(tex, {vid.x + (vid.w - w) * 0.5f, vid.y + (vid.h - h) * 0.5f, w, h});
        }

        rr.draw_rect(ctrl, gfx::theme::STRIP_BG);

        // Play/pause icon at the left.
        const float icon_cx = ctrl.x + PAD + 6.0f;
        const float icon_cy = ctrl.y + CONTROL_H * 0.5f;
        if (model_.playing()) {
            rr.draw_rect({icon_cx - 6.0f, icon_cy - 8.0f, 4.0f, 16.0f}, gfx::theme::TEXT);
            rr.draw_rect({icon_cx + 2.0f, icon_cy - 8.0f, 4.0f, 16.0f}, gfx::theme::TEXT);
        } else {
            rr.draw_triangle({icon_cx - 6.0f, icon_cy - 8.0f},
                             {icon_cx - 6.0f, icon_cy + 8.0f},
                             {icon_cx + 8.0f, icon_cy}, gfx::theme::TEXT);
        }

        // Loop icon (a ring), same visual treatment as the mute speaker icon:
        // dim/faint when off, accent-coloured when the user has enabled
        // looping (Phase 40). Placed right after the play/pause icon.
        constexpr float LOOP_R  = 8.0f;
        const float     loop_cx = icon_cx + 18.0f + LOOP_R;
        loop_icon_ = {loop_cx - LOOP_R, icon_cy - LOOP_R, LOOP_R * 2.0f, LOOP_R * 2.0f};
        rr.draw_round_rect(loop_icon_, LOOP_R, loop_ ? gfx::theme::ACCENT : gfx::theme::TEXT_FAINT,
                           false);

        // "pos / dur" text on the right.
        const std::string label =
            format_clock(model_.position()) + " / " + format_clock(model_.duration());
        const auto  text_w = static_cast<float>(font.measure(label));
        const float text_x = ctrl.x + ctrl.w - PAD - text_w;
        rr.draw_text(font, text_x, font.text_top_for_center(icon_cy), label, gfx::theme::TEXT_DIM);

        // Volume control (speaker icon + draggable bar), laid out right before the
        // time text. Drawn first so the seek track can stop before it (no overlap).
        // Only shown when the clip actually has an open audio device.
        float controls_right = text_x - 12.0f;     // seek track stops before this x
        if (audio_.stream) {
            constexpr float VOL_BAR_W = 56.0f;
            constexpr float VOL_BAR_H = 5.0f;
            constexpr float SPK_W     = 13.0f;
            const float bar_x = text_x - PAD - VOL_BAR_W;
            const float bar_y = icon_cy - VOL_BAR_H * 0.5f;
            vol_.bar = {bar_x, bar_y, VOL_BAR_W, VOL_BAR_H};

            const float spk_x = bar_x - 10.0f - SPK_W;
            vol_.icon = {spk_x, icon_cy - 8.0f, SPK_W, 16.0f};

            // Speaker glyph (body rect + cone triangle); dimmed when muted.
            const gfx::Color spk_c = vol_.muted ? gfx::theme::TEXT_FAINT : gfx::theme::TEXT;
            rr.draw_rect({spk_x, icon_cy - 4.0f, 5.0f, 8.0f}, spk_c);
            rr.draw_triangle({spk_x + 5.0f, icon_cy - 7.0f},
                             {spk_x + 5.0f, icon_cy + 7.0f},
                             {spk_x + SPK_W, icon_cy}, spk_c);

            // Volume bar: background, fill (empty when muted), draggable knob.
            rr.draw_round_rect(vol_.bar, VOL_BAR_H * 0.5f, gfx::theme::BORDER);
            const float level  = vol_.muted ? 0.0f : vol_.level;
            const float fill_w = VOL_BAR_W * level;
            const gfx::Color accent = vol_.muted ? gfx::theme::TEXT_FAINT : gfx::theme::ACCENT;
            if (fill_w > 0.0f)
                rr.draw_round_rect({bar_x, bar_y, fill_w, VOL_BAR_H}, VOL_BAR_H * 0.5f, accent);
            const float vknob_x = bar_x + VOL_BAR_W * level;
            rr.draw_round_rect({vknob_x - KNOB_R, icon_cy - KNOB_R, KNOB_R * 2.0f, KNOB_R * 2.0f},
                               KNOB_R, accent);

            controls_right = spk_x - 12.0f;        // seek track stops before the speaker
        } else {
            vol_.bar = {0.0f, 0.0f, 0.0f, 0.0f};
            vol_.icon  = {0.0f, 0.0f, 0.0f, 0.0f};
        }

        // Seek track between the loop icon and the volume/time region.
        const float track_x = loop_cx + LOOP_R + 10.0f;
        const float track_w = std::max(10.0f, controls_right - track_x);
        track_ = {track_x, icon_cy - TRACK_H * 0.5f, track_w, TRACK_H};
        rr.draw_round_rect(track_, TRACK_H * 0.5f, gfx::theme::BORDER);
        if (const float fill_w = time_to_bar_x(model_.position(), model_.duration(), 0.0f, track_w);
            fill_w > 0.0f)
            rr.draw_round_rect({track_.x, track_.y, fill_w, TRACK_H}, TRACK_H * 0.5f,
                               gfx::theme::ACCENT);
        const float knob_x = time_to_bar_x(model_.position(), model_.duration(), track_.x, track_w);
        rr.draw_round_rect({knob_x - KNOB_R, icon_cy - KNOB_R, KNOB_R * 2.0f, KNOB_R * 2.0f},
                           KNOB_R, gfx::theme::ACCENT);
    }

    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] bool animating() const { return valid_ && model_.playing(); }
    [[nodiscard]] bool has_audio() const { return decoder_.has_audio(); }
    [[nodiscard]] double position() const { return clock(); }
    [[nodiscard]] bool audio_active() const
    {
        return audio_.stream != nullptr;
    }
    [[nodiscard]] uint64_t audio_samples_fed() const
    {
        return audio_.samples_fed;
    }
    [[nodiscard]] float audio_gain() const { return media::effective_gain(vol_.level, vol_.muted); }
    [[nodiscard]] SDL_FRect debug_vol_bar() const { return vol_.bar; }
    [[nodiscard]] size_t debug_in_flight_packets() const { return in_flight_; }
    void update(double dt)
    {
        if (valid_) {
            model_.tick(dt);
            apply_pause_resume();
        }
    }
};

#else  // !OSV_VENDORED_AV — playback unavailable; host falls back to the poster.

struct VideoPlayback::Impl {
    Impl(const vault::Vault&, const vault::IndexNode&, std::chrono::milliseconds) {}
    [[nodiscard]] bool valid() const { return false; }
    [[nodiscard]] bool animating() const { return false; }
    [[nodiscard]] bool has_audio() const { return false; }
    [[nodiscard]] double position() const { return 0.0; }
    void do_seek(double) {}
    [[nodiscard]] bool audio_active() const { return false; }
    [[nodiscard]] uint64_t audio_samples_fed() const { return 0; }
    [[nodiscard]] float audio_gain() const { return 0.0f; }
    [[nodiscard]] SDL_FRect debug_vol_bar() const { return {0.0f, 0.0f, 0.0f, 0.0f}; }
    [[nodiscard]] size_t debug_in_flight_packets() const { return 0; }
    void update(double) {}
    void render(gfx::Renderer&, gfx::FontAtlas&, const SDL_FRect&) {}
    void key(SDL_Keycode, SDL_Scancode) {}
    void mouse_down(float, float) {}
    void mouse_motion(float, float, bool) {}
    void mouse_up() {}
};

#endif  // OSV_VENDORED_AV

VideoPlayback::VideoPlayback(const vault::Vault& vault, const vault::IndexNode& node,
                             std::chrono::milliseconds test_only_decode_delay)
    : impl_(std::make_unique<Impl>(vault, node, test_only_decode_delay)) {}

VideoPlayback::~VideoPlayback() = default;

bool VideoPlayback::valid() const noexcept { return impl_->valid(); }
bool VideoPlayback::animating() const noexcept { return impl_->animating(); }
bool VideoPlayback::has_audio() const noexcept { return impl_->has_audio(); }
double VideoPlayback::position() const noexcept { return impl_->position(); }
void VideoPlayback::seek(double seconds) { impl_->do_seek(seconds); }
bool VideoPlayback::audio_active() const noexcept { return impl_->audio_active(); }
uint64_t VideoPlayback::audio_samples_fed() const noexcept { return impl_->audio_samples_fed(); }
float VideoPlayback::audio_gain() const noexcept { return impl_->audio_gain(); }
SDL_FRect VideoPlayback::debug_vol_bar() const noexcept { return impl_->debug_vol_bar(); }
size_t VideoPlayback::debug_in_flight_packets() const noexcept { return impl_->debug_in_flight_packets(); }
void VideoPlayback::update(double dt) { impl_->update(dt); }

void VideoPlayback::render(gfx::Renderer& r, gfx::FontAtlas& font, const SDL_FRect& area)
{
    impl_->render(r, font, area);
}

void VideoPlayback::handle_key(SDL_Keycode key, SDL_Scancode sc) { impl_->key(key, sc); }
void VideoPlayback::handle_mouse_down(float mx, float my) { impl_->mouse_down(mx, my); }
void VideoPlayback::handle_mouse_motion(float mx, float my, bool left_down)
{
    impl_->mouse_motion(mx, my, left_down);
}
void VideoPlayback::handle_mouse_up() { impl_->mouse_up(); }

}  // namespace ui
