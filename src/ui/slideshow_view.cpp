#include "ui/slideshow_view.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "ui/full_tex_cache.h"
#include "ui/viewer_model.h"   // wrap_index, fit_zoom, clamp_zoom
#include "ui/widgets.h"
#include "vault/index.h"

namespace ui {

namespace {
// Centre an (iw x ih) image fit-to-window inside viewport `vp` (letterboxed).
SDL_FRect fit_dest(float iw, float ih, const SDL_FRect& vp)
{
    const float z  = clamp_zoom(fit_zoom(iw, ih, vp.w, vp.h));
    const float sw = iw * z;
    const float sh = ih * z;
    return SDL_FRect{vp.x + (vp.w - sw) * 0.5f, vp.y + (vp.h - sh) * 0.5f, sw, sh};
}
} // namespace

void SlideshowView::start(int count, int start_index)
{
    show_.emplace(count, start_index, dwell_, shuffle_,
                  static_cast<uint64_t>(SDL_GetTicks()));
}

void SlideshowView::reseed(bool keep_running)
{
    if (!show_) return;
    show_.emplace(show_->count(), show_->index(), dwell_, shuffle_,
                  static_cast<uint64_t>(SDL_GetTicks()));
    show_->set_running(keep_running);
}

bool SlideshowView::handle_key(SDL_Keycode key, SDL_Scancode sc)
{
    if (!show_) return false;

    // `[` / `]` adjust the dwell. Match by physical scancode (ui/keybindings.h) so
    // they work on layouts where those glyphs sit behind AltGr (Phase 25); the
    // `-` / `+` glyph keys below remain as layout-friendly alternatives.
    using enum BracketKey;
    switch (bracket_key_for_scancode(sc)) {
        case Decrease: show_->adjust_dwell(-SLIDESHOW_DWELL_STEP);
                       dwell_ = show_->dwell(); return true;
        case Increase: show_->adjust_dwell(SLIDESHOW_DWELL_STEP);
                       dwell_ = show_->dwell(); return true;
        case None:     break;
    }

    switch (key) {
        case SDLK_P:
        case SDLK_SPACE:  show_->toggle();  break;   // play / pause
        case SDLK_ESCAPE:
        case SDLK_UP:     return false;              // exit to the still viewer
        case SDLK_RIGHT:  show_->advance(1);  break;
        case SDLK_LEFT:   show_->advance(-1); break;
        case SDLK_S:      shuffle_ = !shuffle_; reseed(show_->running()); break;
        case SDLK_MINUS:
        case SDLK_KP_MINUS: show_->adjust_dwell(-SLIDESHOW_DWELL_STEP);
                            dwell_ = show_->dwell(); break;
        case SDLK_PLUS:
        case SDLK_EQUALS:
        case SDLK_KP_PLUS:  show_->adjust_dwell(SLIDESHOW_DWELL_STEP);
                            dwell_ = show_->dwell(); break;
        default: break;
    }
    return true;
}

void SlideshowView::render(gfx::Renderer& r, gfx::FontAtlas& font, FullTexCache& cache,
                           std::span<const vault::IndexNode* const> images,
                           float win_w, float win_h)
{
    const SDL_FRect vp{0.0f, 0.0f, win_w, win_h};
    r.draw_rect(vp, gfx::theme::IMG_BG);
    if (!show_ || images.empty()) return;

    const auto n    = static_cast<int>(images.size());
    const int  cur  = std::clamp(show_->index(), 0, n - 1);
    const int  nxt  = wrap_index(cur, 1, n);
    const int  prev = show_->prev_index();   // outgoing during a cross-fade, else -1

    // Decode the current, outgoing and next frames (next is prefetched so the
    // upcoming advance is seamless); evict everything else to keep a bounded set.
    FullTex* cur_ft  = cache.acquire(*images[cur]);
    FullTex* prev_ft = prev >= 0 ? cache.acquire(*images[prev]) : nullptr;
    cache.acquire(*images[nxt]);
    keep_scratch_.clear();
    keep_scratch_.push_back(images[cur]->meta.data_offset);
    keep_scratch_.push_back(images[nxt]->meta.data_offset);
    if (prev >= 0) keep_scratch_.push_back(images[prev]->meta.data_offset);
    cache.evict_except(keep_scratch_);

    const SDL_Rect clip{static_cast<int>(vp.x), static_cast<int>(vp.y),
                        static_cast<int>(vp.w), static_cast<int>(vp.h)};
    SDL_SetRenderClipRect(r.sdl(), &clip);

    // Cross-fade: the outgoing frame is drawn opaque, the incoming frame on top at
    // alpha = fade_progress, so the blend resolves to in*p + out*(1-p).
    const auto p = static_cast<float>(show_->fade_progress());
    if (prev_ft)
        r.draw_image(prev_ft->tex, fit_dest(prev_ft->w, prev_ft->h, vp));
    if (cur_ft) {
        const auto a = static_cast<uint8_t>(std::clamp(p, 0.0f, 1.0f) * 255.0f + 0.5f);
        r.draw_image(cur_ft->tex, fit_dest(cur_ft->w, cur_ft->h, vp),
                     gfx::Color{255, 255, 255, prev_ft ? a : static_cast<uint8_t>(255)});
    } else if (!cache.error().empty()) {
        r.draw_text(font, vp.x + 20, vp.y + vp.h * 0.5f, cache.error(), gfx::theme::DANGER);
    }
    SDL_SetRenderClipRect(r.sdl(), nullptr);

    // Status + controls HUD.
    const char* state = show_->running() ? "Playing" : "Paused";
    // Elide the (unbounded) image name so the fixed trailing status always fits.
    const std::string tail =
        std::format("   {} of {}   {}   {:.0f}s per slide{}",
                    cur + 1, n, state, show_->dwell(), shuffle_ ? "   Shuffle on" : "");
    const std::string hud =
        fit_text(font, images[cur]->name,
                 vp.w - 32 - static_cast<float>(font.measure(tail))) + tail;
    r.draw_text(font, vp.x + 16, vp.y + 12, hud, gfx::theme::TEXT);
    r.draw_text(font, vp.x + 16, vp.y + 44,
                "[Space] Play/Pause   [<-/->] Prev/Next   [+/-] Slower/Faster   "
                "[S] Shuffle   [Esc] Exit",
                gfx::theme::TEXT_FAINT);
}

} // namespace ui
