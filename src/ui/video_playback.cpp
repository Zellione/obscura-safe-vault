#include "ui/video_playback.h"

#include <algorithm>
#include <string>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "ui/playback_model.h"
#include "ui/viewer_model.h"   // fit_zoom

#ifdef OSV_VENDORED_AV
#include <memory>
#include <optional>
#include <print>

#include "gfx/yuv_texture.h"
#include "media/av_sync.h"
#include "media/chunk_avio.h"
#include "media/video_decoder.h"
#include "media/video_source.h"
#include "vault/index.h"
#include "vault/vault.h"
#endif

namespace ui {

namespace {
constexpr float  CONTROL_H        = 56.0f;       // transport strip height
constexpr float  PAD              = 16.0f;
constexpr float  TRACK_H          = 6.0f;
constexpr float  KNOB_R           = 8.0f;
constexpr double DEFAULT_FRAME_DT = 1.0 / 30.0;  // backward frame-step fallback
}  // namespace

#ifdef OSV_VENDORED_AV

// Full implementation: a ChunkAvio over the vault's encrypted chunks feeds a
// VideoDecoder; decoded YUV frames upload to a streaming texture. The pure
// PlaybackModel owns the transport clock. No bytes touch disk (invariant #1).
struct VideoPlayback::Impl {
    media::VideoDecoder               decoder_;
    std::unique_ptr<media::ChunkAvio> avio_;
    gfx::YuvTexture                   yuv_;
    PlaybackModel                     model_{0.0};
    bool                              valid_ = false;

    std::optional<media::DecodedFrame> pending_;        // next decoded frame (planes valid)
    double    pending_pts_  = 0.0;
    double    shown_pts_    = -1.0;                      // last presented frame's pts
    double    frame_dt_     = DEFAULT_FRAME_DT;          // estimated, for backward step
    bool      need_present_ = false;                     // force-show pending_ (open / seek)
    bool      eof_          = false;
    bool      scrubbing_    = false;
    SDL_FRect track_{};                                  // last-rendered seek-bar track rect

    // Audio state
    SDL_AudioStream* audio_       = nullptr;
    uint64_t         samples_fed_ = 0;
    double           seek_base_   = 0.0;
    float            volume_      = 1.0f;
    bool             muted_       = false;

    Impl(const vault::Vault& vault, const vault::IndexNode& node)
    {
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
        model_ = PlaybackModel(static_cast<double>(decoder_.duration_us()) / 1'000'000.0);

        // Open audio device if available
        if (decoder_.has_audio()) {
            SDL_AudioSpec src{};
            src.format   = SDL_AUDIO_F32;
            src.channels = decoder_.audio_info().channels;
            src.freq     = decoder_.audio_info().sample_rate;
            audio_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &src,
                                               nullptr, nullptr);
            if (!audio_) {
                std::println(stderr, "[VideoPlayback] audio open failed: {}", SDL_GetError());
            } else {
                SDL_SetAudioStreamGain(audio_, media::effective_gain(volume_, muted_));
            }
        }

        decode_into_pending();   // first frame ready; uploaded lazily on first render
        need_present_ = true;    // show frame 0 immediately, paused
    }

    ~Impl()
    {
        if (audio_) SDL_DestroyAudioStream(audio_);
    }

    void decode_into_pending()
    {
        pending_ = decoder_.next_frame();
        if (pending_) {
            const double p = pending_->pts_seconds;
            if (shown_pts_ >= 0.0 && p > shown_pts_)
                frame_dt_ = std::max(1e-3, p - shown_pts_);
            pending_pts_ = p;
        } else {
            eof_ = true;
        }
    }

    void pump_audio()
    {
        if (!audio_) return;
        const int ch = decoder_.audio_info().channels;
        const int sr = decoder_.audio_info().sample_rate;
        const int target = sr * ch * (int)sizeof(float) / 5;   // ~200ms buffered
        while (SDL_GetAudioStreamQueued(audio_) < target) {
            auto a = decoder_.next_audio_frame();
            if (!a) break;
            SDL_PutAudioStreamData(audio_, a->samples.data(),
                                   (int)(a->samples.size() * sizeof(float)));
            samples_fed_ += a->samples.size() / (ch > 0 ? ch : 1);
        }
    }

    double clock() const
    {
        if (!audio_) return model_.position();
        const int ch = decoder_.audio_info().channels;
        const int sr = decoder_.audio_info().sample_rate;
        uint64_t queued_samples =
            (uint64_t)SDL_GetAudioStreamQueued(audio_) / (sizeof(float) * (ch > 0 ? ch : 1));
        uint64_t consumed = samples_fed_ > queued_samples ? samples_fed_ - queued_samples : 0;
        return media::audio_clock(seek_base_, consumed, sr);
    }

    // Upload pending_ to the GPU (durable copy) then decode the next frame into it.
    void present_pending(SDL_Renderer* r)
    {
        if (!pending_) return;
        if (yuv_.ensure(r, pending_->width, pending_->height, pending_->pix_fmt))
            (void)yuv_.update(*pending_);
        shown_pts_    = pending_pts_;
        need_present_ = false;
        decode_into_pending();
    }

    // Decode + upload frames using audio clock (if available) or model clock fallback.
    void advance(SDL_Renderer* r)
    {
        pump_audio();  // Ensure audio buffer is topped up

        const double c = clock();
        // Sync model position toward audio clock when audio is active
        if (audio_ && model_.playing()) {
            model_.seek_to(c);
        }

        // Drive video frames against the clock using av_sync
        while (pending_) {
            if (need_present_) {
                present_pending(r);
                need_present_ = false;
            } else {
                media::FrameAction act = media::decide(c, pending_pts_);
                if (act == media::FrameAction::Hold) {
                    break;
                } else if (act == media::FrameAction::Drop) {
                    decode_into_pending();
                } else {  // Present
                    present_pending(r);
                }
            }
        }

        if (eof_ && !pending_ && model_.playing())
            model_.set_playing(false);   // pause at end of stream
    }

    void do_seek(double t)
    {
        const double tt = clamp_time(t, model_.duration());
        if (!decoder_.seek(tt)) return;
        if (audio_) SDL_ClearAudioStream(audio_);
        samples_fed_ = 0;
        seek_base_   = tt;
        pending_.reset();
        eof_       = false;
        shown_pts_ = -1.0;
        decode_into_pending();
        model_.seek_to(pending_ ? pending_pts_ : tt);
        need_present_ = true;
    }

    void apply_pause_resume()
    {
        if (audio_) {
            if (model_.playing()) SDL_ResumeAudioStreamDevice(audio_);
            else                  SDL_PauseAudioStreamDevice(audio_);
        }
    }

    // Helper for Task 6: apply volume/mute state to audio stream
    // Task 6 will add key bindings for M, [, ] to change volume_/muted_ and call this
    void apply_gain()
    {
        if (audio_) {
            SDL_SetAudioStreamGain(audio_, media::effective_gain(volume_, muted_));
        }
    }

    void key(SDL_Keycode k)
    {
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
                do_seek(model_.position() - frame_dt_);
                break;
            case SDLK_PERIOD:  // step forward one frame (paused)
                model_.set_playing(false);
                apply_pause_resume();
                if (pending_) {
                    model_.seek_to(pending_pts_);
                    need_present_ = true;
                }
                break;
            case SDLK_J: do_seek(model_.position() - PLAYBACK_SEEK_STEP); break;
            case SDLK_L: do_seek(model_.position() + PLAYBACK_SEEK_STEP); break;
            case SDLK_M:
                muted_ = !muted_;
                apply_gain();
                break;
            case SDLK_LEFTBRACKET:
                volume_ = media::clamp_volume(volume_ - 0.05f);
                apply_gain();
                break;
            case SDLK_RIGHTBRACKET:
                volume_ = media::clamp_volume(volume_ + 0.05f);
                apply_gain();
                break;
            default: break;
        }
    }

    [[nodiscard]] bool on_track(float mx, float my) const
    {
        constexpr float grab_y = 12.0f;   // generous vertical grab margin
        return mx >= track_.x - KNOB_R && mx <= track_.x + track_.w + KNOB_R &&
               my >= track_.y - grab_y && my <= track_.y + track_.h + grab_y;
    }

    void mouse_down(float mx, float my)
    {
        if (!on_track(mx, my)) return;
        scrubbing_ = true;
        do_seek(bar_x_to_time(mx, model_.duration(), track_.x, track_.w));
    }

    void mouse_motion(float mx, float /*my*/, bool left_down)
    {
        if (scrubbing_ && left_down)
            do_seek(bar_x_to_time(mx, model_.duration(), track_.x, track_.w));
    }

    void mouse_up() { scrubbing_ = false; }

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

        // "pos / dur" text on the right.
        const std::string label =
            format_clock(model_.position()) + " / " + format_clock(model_.duration());
        const auto  text_w = static_cast<float>(font.measure(label));
        const float text_x = ctrl.x + ctrl.w - PAD - text_w;
        rr.draw_text(font, text_x, font.text_top_for_center(icon_cy), label, gfx::theme::TEXT_DIM);

        // Seek track between the icon and the time text.
        const float track_x = icon_cx + 18.0f;
        const float track_w = std::max(10.0f, text_x - 12.0f - track_x);
        track_ = {track_x, icon_cy - TRACK_H * 0.5f, track_w, TRACK_H};
        rr.draw_round_rect(track_, TRACK_H * 0.5f, gfx::theme::BORDER);
        if (const float fill_w = time_to_bar_x(model_.position(), model_.duration(), 0.0f, track_w);
            fill_w > 0.0f)
            rr.draw_round_rect({track_.x, track_.y, fill_w, TRACK_H}, TRACK_H * 0.5f,
                               gfx::theme::ACCENT);
        const float knob_x = time_to_bar_x(model_.position(), model_.duration(), track_.x, track_w);
        rr.draw_round_rect({knob_x - KNOB_R, icon_cy - KNOB_R, KNOB_R * 2.0f, KNOB_R * 2.0f},
                           KNOB_R, gfx::theme::ACCENT);

        // Volume indicator (only when audio is available)
        if (audio_) {
            constexpr float VOL_BAR_W = 32.0f;
            constexpr float VOL_BAR_H = 4.0f;
            const float vol_x = text_x - VOL_BAR_W - 12.0f;
            const float vol_y = icon_cy - VOL_BAR_H * 0.5f;

            // Background track
            rr.draw_round_rect({vol_x, vol_y, VOL_BAR_W, VOL_BAR_H}, VOL_BAR_H * 0.5f,
                               gfx::theme::BORDER);

            // Fill based on volume level
            const float fill_w = VOL_BAR_W * volume_;
            const gfx::Color fill_color = muted_ ? gfx::theme::TEXT_FAINT : gfx::theme::ACCENT;
            if (fill_w > 0.0f)
                rr.draw_round_rect({vol_x, vol_y, fill_w, VOL_BAR_H}, VOL_BAR_H * 0.5f,
                                   fill_color);

            // Draw mute indicator when muted
            if (muted_) {
                const float mute_text_x = vol_x - 20.0f;
                rr.draw_text(font, mute_text_x, font.text_top_for_center(icon_cy), "M",
                             gfx::theme::TEXT_DIM);
            }
        }
    }

    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] bool animating() const { return valid_ && model_.playing(); }
    [[nodiscard]] bool has_audio() const { return decoder_.has_audio(); }
    [[nodiscard]] double position() const { return clock(); }
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
    Impl(const vault::Vault&, const vault::IndexNode&) {}
    [[nodiscard]] bool valid() const { return false; }
    [[nodiscard]] bool animating() const { return false; }
    [[nodiscard]] bool has_audio() const { return false; }
    [[nodiscard]] double position() const { return 0.0; }
    void update(double) {}
    void render(gfx::Renderer&, gfx::FontAtlas&, const SDL_FRect&) {}
    void key(SDL_Keycode) {}
    void mouse_down(float, float) {}
    void mouse_motion(float, float, bool) {}
    void mouse_up() {}
};

#endif  // OSV_VENDORED_AV

VideoPlayback::VideoPlayback(const vault::Vault& vault, const vault::IndexNode& node)
    : impl_(std::make_unique<Impl>(vault, node)) {}

VideoPlayback::~VideoPlayback() = default;

bool VideoPlayback::valid() const noexcept { return impl_->valid(); }
bool VideoPlayback::animating() const noexcept { return impl_->animating(); }
bool VideoPlayback::has_audio() const noexcept { return impl_->has_audio(); }
double VideoPlayback::position() const noexcept { return impl_->position(); }
void VideoPlayback::update(double dt) { impl_->update(dt); }

void VideoPlayback::render(gfx::Renderer& r, gfx::FontAtlas& font, const SDL_FRect& area)
{
    impl_->render(r, font, area);
}

void VideoPlayback::handle_key(SDL_Keycode key) { impl_->key(key); }
void VideoPlayback::handle_mouse_down(float mx, float my) { impl_->mouse_down(mx, my); }
void VideoPlayback::handle_mouse_motion(float mx, float my, bool left_down)
{
    impl_->mouse_motion(mx, my, left_down);
}
void VideoPlayback::handle_mouse_up() { impl_->mouse_up(); }

}  // namespace ui
