#include "ui/image_viewer.h"

#include <algorithm>
#include <array>
#include <format>

#include "crypto/secure_mem.h"
#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/texture_cache.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "platform/perf.h"
#include "platform/safe_print.h"
#include "ui/album_rebind.h"
#include "ui/export.h"
#include "ui/anim_model.h"
#include "ui/input.h"
#include "ui/meta_format.h"
#include "ui/strip_layout.h"
#include "ui/tile_thumb.h"
#include "ui/widgets.h"
#include "vault/index.h"
#include "vault/vault.h"

namespace ui {

namespace {
constexpr float PAN_STEP    = 64.0f;   // arrow-key pan distance (px)
constexpr float ZOOM_STEP   = 1.25f;   // keyboard +/- zoom factor
constexpr float WHEEL_STEP  = 1.10f;   // per-notch wheel zoom factor (fit mode)
constexpr float SCROLL_STEP = 96.0f;   // arrow-key / wheel scroll distance (px)

// True when album item `idx` is a video — picks the video render/input path.
bool item_is_video(const std::vector<const vault::IndexNode*>& imgs, int idx)
{
    return idx >= 0 && idx < static_cast<int>(imgs.size()) && imgs[idx]->is_video();
}

bool item_is_animated(const std::vector<const vault::IndexNode*>& imgs, int idx)
{
    if (idx < 0 || idx >= static_cast<int>(imgs.size())) return false;
    const vault::IndexNode* node = imgs[idx];
    return node != nullptr && node->is_image() && node->meta.animated;
}

// Header band: two text lines — the HUD line (name / index / zoom) at +12 and
// "[F1] Help" at +44 — plus a little breathing room under the second.
float hud_header_h(float line_h) noexcept { return 44.0f + line_h + 8.0f; }

// Footer band: one padded text line.
float hud_footer_h(float line_h) noexcept { return line_h + 20.0f; }
} // namespace

ImageViewer::ImageViewer(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                         gfx::TextureCache& cache, Context ctx, Album album, int start_index)
    : win_(win), font_(font), vault_(vault), cache_(cache), queue_(ctx.queue),
      export_(ctx.folder, win),
      tag_editor_(vault, win), search_(vault, win),
      quick_switch_(ctx.registry, std::move(ctx.active_path)),
      album_(std::move(album)), index_(start_index),
      strip_side_(ctx.initial_strip_side),
      full_cache_(vault, win.sdl_renderer(), &decode_worker_)
{
}

void ImageViewer::on_enter()
{
    // Gallery mode snapshots the gallery's media children (a mixed gallery also
    // holds sub-galleries — Phase 46). Collection mode (e.g. favorites) was
    // handed an explicit image set + parallel paths at construction — don't
    // re-list.
    if (!album_.from_collection) {
        const std::vector<const vault::IndexNode*> listing = vault_.list(album_.gallery_path);
        album_.images.clear();
        album_.paths.clear();
        for (const vault::IndexNode* n : listing) {
            if (!n->is_media()) continue;   // images and videos (mixed leaf galleries)
            album_.images.push_back(n);
            album_.paths.push_back(album_.gallery_path.empty() ? n->name
                                                   : album_.gallery_path + "/" + n->name);
        }
        // The grid and the search screens hand over an index into the FULL
        // listing (sub-galleries partition first); the album above is media
        // only. Convert, or every item after the sub-gallery block opens
        // shifted by the number of sub-galleries.
        index_ = media_index_in_listing(listing, index_);
    }

    if (album_.images.empty()) { go_back(); return; }
    show_image_at(index_);   // clamp + refit + (re)build video for the current item
}

void ImageViewer::on_exit()
{
    video_.reset();                 // stop decode + wipe the VideoSource cache first
    anim_.reset();                   // stop animation playback
    strip_hover_anim_.reset();       // stop strip hover animation (Phase 47 Task 10)
    strip_hover_anim_index_ = -1;
    strip_hover_gate_.reset();
    full_cache_.evict_except({});   // destroy all cached textures
}

void ImageViewer::on_vault_changed()
{
    // Phase 50: the vault's index tree changed (a background import drain
    // attached nodes), so album_.images pointers are stale and must be re-listed.
    // Phase 56: re-listing must not disturb what the user is looking at. The
    // current item is re-found by PATH, and when it is still there we assign
    // index_ directly instead of calling show_image_at() — that call refits
    // (fit_.fitted = false), re-runs scroll_to_image() and rebuilds video_/anim_,
    // which is exactly the zoom/scroll/playback reset this fixes. Keeping the
    // decoder alive is safe: VideoSource copies the node's chunk table at open
    // and never retains the IndexNode.
    const std::string current = album_.paths.empty() || index_ < 0 ||
                                index_ >= static_cast<int>(album_.paths.size())
        ? std::string{}
        : album_.paths[static_cast<size_t>(index_)];

    if (!album_.from_collection) {
        album_.images.clear();
        album_.paths.clear();
        for (const vault::IndexNode* n : vault_.list(album_.gallery_path)) {
            if (!n->is_media()) continue;
            album_.images.push_back(n);
            album_.paths.push_back(album_.gallery_path.empty()
                                       ? n->name
                                       : album_.gallery_path + "/" + n->name);
        }
    } else {
        // Phase 58: collection albums hold nodes from all over the tree; a
        // drain may have reallocated any of them. Re-resolve every path
        // against the fresh tree and drop entries that vanished — the old
        // pointers must not survive this call (dangling after realloc).
        for (size_t i = 0; i < album_.images.size(); ++i)
            album_.images[i] = vault_.resolve_node(album_.paths[i]);
        compact_album(album_.images, album_.paths);
        if (album_.images.empty()) { go_back(); return; }
    }

    if (const AlbumRebind rebind = rebind_album_index(album_.paths, current, index_); rebind.preserve) {
        index_ = rebind.index;
    } else {
        if (platform::perf_log_enabled())
            platform::safe_println(stderr,
                "[Viewer] rebind fell back to show_image_at (collection={})",
                album_.from_collection);
        show_image_at(rebind.index);
    }
    mark_dirty();
}

// --- Geometry --------------------------------------------------------------

float ImageViewer::thumb_size() const
{
    return strip_thumb_size(static_cast<float>(win_.height()));
}

SDL_FRect ImageViewer::viewport_rect() const
{
    return viewer_chrome(*this).content;
}

std::string viewer_footer_text(const ImageViewer& v)
{
    // Priority: export status > import summary > AV notice
    // Export status wins: it is transient and the user just asked for it,
    // so it must not be swallowed by a standing message (Phase 50).
    if (!v.export_.status().empty()) return v.export_.status();
    // Phase 50: import summary shows queue progress
    if (const std::string summary = v.queue_.footer_summary(); !summary.empty())
        return summary;
    // AV notice for video codec unavailability
    if (item_is_video(v.album_.images, v.index_) && !(v.video_ && v.video_->valid()))
        return "Video playback unavailable in this build.";
    return {};
}

ChromeBands viewer_chrome(const ImageViewer& v)
{
    const auto w = static_cast<float>(v.win_.width());
    const auto h = static_cast<float>(v.win_.height());
    const bool fs = v.win_.is_fullscreen();

    // Fullscreen reclaims the space the thumbnail strip would occupy — it's
    // hidden outright (render()/strip_hit() both check is_fullscreen() too), so
    // the area is simply the whole window (Phase 45 Part 4).
    const SDL_FRect area = fs ? SDL_FRect{0.0f, 0.0f, w, h}
                              : viewport_rect_for(v.strip_side_, w, h, v.thumb_size());

    // Fullscreen is edge-to-edge: no header at all, and a footer band only while
    // there is actually a message that would otherwise sit unreadable on the image.
    const float line   = v.font_.pixel_height();
    const float header = fs ? 0.0f : hud_header_h(line);
    const float footer = (!fs || !viewer_footer_text(v).empty()) ? hud_footer_h(line) : 0.0f;
    return split_chrome(area, header, footer);
}

SDL_FRect ImageViewer::strip_rect() const
{
    return strip_rect_for(strip_side_, static_cast<float>(win_.width()),
                          static_cast<float>(win_.height()), thumb_size());
}

// --- Fit-mode view state ---------------------------------------------------

void ImageViewer::sync_anim_for_current_index()
{
    // Rebuild anim_ when index_ has moved away from anim_index_, or tear it down
    // when the current item is no longer an animated image. Never tear down and
    // rebuild if index_ hasn't changed (that would restart the animation every
    // frame).
    if (anim_index_ == index_) {
        return;   // the animation is still valid for the current index
    }

    // Index has changed or anim_ was never built — rebuild if the current item
    // is an animated image, otherwise tear down.
    anim_.reset();
    anim_index_ = -1;

    if (item_is_animated(album_.images, index_)) {
        anim_ = std::make_unique<AnimPlayback>(vault_, *album_.images[index_]);
        anim_index_ = index_;
    }
}

void ImageViewer::start_strip_hover_animation(int strip_thumb)
{
    // Resolve the node at this thumbnail index.
    if (strip_thumb < 0 || strip_thumb >= static_cast<int>(album_.images.size())) {
        strip_hover_anim_.reset();
        strip_hover_anim_index_ = -1;
        return;
    }

    const vault::IndexNode* node = album_.images[strip_thumb];
    if (!node) {
        strip_hover_anim_.reset();
        strip_hover_anim_index_ = -1;
        return;
    }

    // Check if this tile can be animated on hover: must have the animated badge
    // and dimensions within the hover budget.
    if (!tile_can_hover_animate(*node)) {
        strip_hover_anim_.reset();
        strip_hover_anim_index_ = -1;
        return;
    }

    // Construct the playback decoder.
    auto playback = std::make_unique<AnimPlayback>(vault_, *node);

    // Verify it's valid.
    if (!playback->valid()) {
        strip_hover_anim_.reset();
        strip_hover_anim_index_ = -1;
        return;
    }

    // All checks passed; keep the playback alive.
    strip_hover_anim_ = std::move(playback);
    strip_hover_anim_index_ = strip_thumb;
}

void ImageViewer::show_image_at(int idx)
{
    if (album_.images.empty()) return;
    index_    = std::clamp(idx, 0, static_cast<int>(album_.images.size()) - 1);
    fit_.fitted   = false;
    fit_.dragging = false;
    if (mode_ == ViewMode::FillScroll) scroll_to_image(index_);
    // (Re)build live playback for the current item only when it is a video in Fit
    // mode; tears down the previous decoder (RAII) before any vault lock. video_
    // being non-null thus means exactly "Fit mode, current item is a video".
    video_.reset();
    if (mode_ == ViewMode::Fit && item_is_video(album_.images, index_)) {
        video_ = std::make_unique<VideoPlayback>(vault_, *album_.images[index_]);
    }
    // Sync animated image playback for the current item; tears down the previous
    // decoder (RAII) before any vault lock if the index has changed.
    sync_anim_for_current_index();
}

void ImageViewer::handle_key_video(SDL_Keycode key, SDL_Scancode sc)
{
    switch (key) {
        case SDLK_F:   // lift the video out of fill-scroll so it can play
            if (mode_ == ViewMode::FillScroll) { mode_ = ViewMode::Fit; show_image_at(index_); }
            return;
        case SDLK_LEFT:  set_index(-1); return;
        case SDLK_RIGHT: set_index(1);  return;
        case SDLK_UP:    go_back();     return;
        default:         if (video_) video_->handle_key(key, sc); return;   // Space/,/./J/L + [ ] volume
    }
}

bool ImageViewer::handle_shared_key(SDL_Keycode key)
{
    using enum StripSide;
    using enum ViewMode;
    switch (key) {
        case SDLK_T:      // toggle the strip between bottom and left
            strip_side_ = (strip_side_ == Bottom) ? Left : Bottom;
            fit_.fitted = false;                      // viewport changed: refit
            if (mode_ == FillScroll) { scroll_to_image(index_); }
            return true;
        case SDLK_G:      // edit tags for the current item
            if (!album_.images.empty()) { tag_editor_.open(album_.paths[index_]); }
            return true;
        case SDLK_B:      // toggle favorite (bookmark) on the current item
            // best-effort: favorite toggle failure is benign, UI re-reads state
            if (!album_.images.empty()) { (void)vault_.toggle_favorite(album_.paths[index_]); }
            return true;
        case SDLK_ESCAPE:
            if (win_.is_fullscreen()) { win_.set_fullscreen(false); return true; }
            go_back();
            return true;
        case SDLK_F11:
            win_.set_fullscreen(!win_.is_fullscreen());
            return true;
        case SDLK_U:      // keep the vault unlocked for the rest of the session
            request(NavKind::ToggleKeepUnlocked);
            return true;
        default:
            return false;
    }
}

bool ImageViewer::handle_key_anim(SDL_Keycode key)
{
    // Space toggles pause; every other key falls through to normal image
    // handling (navigation, fit/scroll, ...), so an animation still navigates.
    if (anim_viewer_consumes_key(key) && anim_ && anim_->valid()) {
        anim_->toggle_pause();
        return true;
    }
    return false;
}

void ImageViewer::set_index(int delta)
{
    show_image_at(wrap_index(index_, delta, static_cast<int>(album_.images.size())));
}

void ImageViewer::go_back()
{
    // Collection mode returns to wherever it was launched from (e.g. the favorites
    // grid); gallery mode returns to its gallery at the current item. The grid
    // selects by FULL-listing index, so the media-only album index converts back
    // (the inverse of the on_enter conversion).
    if (album_.from_collection) {
        request(album_.back.kind, album_.back.path, album_.back.index);
    } else {
        request(NavKind::ToGallery, album_.gallery_path,
                listing_index_of_media(vault_.list(album_.gallery_path), index_));
    }
}

void ImageViewer::zoom_by(float factor, float cx, float cy)
{
    const SDL_FRect vp = viewport_rect();
    const ZoomResult z = zoom_at(fit_.img_size, fit_.zoom, fit_.pan, factor,
                                 Vec2{cx - vp.x, cy - vp.y}, Vec2{vp.w, vp.h});
    fit_.zoom = z.zoom;
    fit_.pan  = z.pan;
}

void ImageViewer::pan_by(float dx, float dy)
{
    const SDL_FRect vp = viewport_rect();
    fit_.pan = clamp_pan(Vec2{fit_.pan.x + dx, fit_.pan.y + dy},
                         fit_.img_size.x * fit_.zoom, fit_.img_size.y * fit_.zoom,
                         vp.w, vp.h);
}

// --- FillScroll-mode helpers ----------------------------------------------

float ImageViewer::scaled_height(const vault::IndexNode& n, float vp_w) const
{
    const float w = n.meta.width  ? static_cast<float>(n.meta.width)  : 1.0f;
    const float h = n.meta.height ? static_cast<float>(n.meta.height) : 1.0f;
    return vp_w * (h / w);
}

const ScrollModel& ImageViewer::build_scroll_model()
{
    if (const SDL_FRect vp = viewport_rect();
        !scroll_cache_.model || scroll_cache_.w != vp.w || scroll_cache_.h != vp.h ||
        scroll_cache_.n != album_.images.size()) {
        std::vector<float> heights;   // transient; ScrollModel copies into prefix sums
        heights.reserve(album_.images.size());
        for (const vault::IndexNode* n : album_.images) heights.push_back(scaled_height(*n, vp.w));
        scroll_cache_.model.emplace(heights, vp.h);
        scroll_cache_.w = vp.w;
        scroll_cache_.h = vp.h;
        scroll_cache_.n = album_.images.size();
    }
    return *scroll_cache_.model;
}

void ImageViewer::scroll_to_image(int idx)
{
    const ScrollModel& m = build_scroll_model();
    scroll_y_ = m.clamp_scroll(m.image_top(std::clamp(idx, 0, m.count() - 1)));
}

void ImageViewer::scroll_by(float dy)
{
    const ScrollModel& m = build_scroll_model();
    scroll_y_ = m.clamp_scroll(scroll_y_ + dy);
    index_    = m.active_index(scroll_y_);
}

// --- Thumbnail strip -------------------------------------------------------

SDL_Texture* ImageViewer::thumb_texture(const vault::IndexNode& node)
{
    const ui::ThumbKey k = ui::thumb_key_for(node);
    if (!k.present) return nullptr;
    if (SDL_Texture* t = cache_.get(k.key)) return t;

    // Phase 58: async fetch via worker thread (thread-safe vault read).
    if (thumbs_.failed.contains(k.key) || thumbs_.worker.pending(k.key)) return nullptr;
    thumbs_.worker.submit_fetch(k.key,
        [&v = vault_, off = k.offset, len = k.length](crypto::SecureBytes& out) {
            return vault::read_thumb_span(v, off, len, out) == vault::VaultResult::Ok;
        });
    return nullptr;
}

bool ImageViewer::pump_thumbs()
{
    bool any = false;
    while (auto res = thumbs_.worker.take_result()) {
        if (res->image) {
            cache_.get_or_upload(res->key, *res->image);
            any = true;
        } else {
            thumbs_.failed.insert(res->key);
        }
    }
    return any;
}

int ImageViewer::strip_hit(float mx, float my) const
{
    if (win_.is_fullscreen()) return -1;   // strip is hidden — nothing to hit (Phase 45 Part 4)
    const SDL_FRect strip   = strip_rect();
    const float     thumb   = thumb_size();
    const bool      vertical = (strip_side_ == StripSide::Left);

    const float cross0 = vertical ? strip.x + (strip.w - thumb) * 0.5f
                                   : strip.y + (strip.h - thumb) * 0.5f;
    if (const float cross = vertical ? mx : my; cross < cross0 || cross > cross0 + thumb)
        return -1;

    const float extent = vertical ? strip.h : strip.w;
    const float scroll = strip_scroll_centered(index_, static_cast<int>(album_.images.size()),
                                               thumb, STRIP_GAP, extent);
    const float along  = vertical ? my : mx;
    const float origin = vertical ? strip.y : strip.x;
    return strip_hit_axis(along, origin, scroll, thumb, STRIP_GAP,
                          static_cast<int>(album_.images.size()));
}

// --- Input -----------------------------------------------------------------

void ImageViewer::handle_key(SDL_Keycode key, SDL_Scancode sc)
{
    using enum StripSide;
    using enum ViewMode;
    if (mode_ == Slideshow) {
        if (!slideshow_.handle_key(key, sc)) {   // user exited the slideshow
            index_ = slideshow_.index();
            slideshow_.stop();
            mode_  = Fit;
            show_image_at(index_);           // refit + (re)build video for the landed item
        }
        return;
    }
    // Keys shared by images and videos.
    if (handle_shared_key(key)) { return; }
    if (item_is_video(album_.images, index_)) { handle_key_video(key, sc); return; }
    // An animation only claims Space (pause); any other key falls through to the image
    // keys below so arrows still navigate between items.
    if (item_is_animated(album_.images, index_) && handle_key_anim(key)) { return; }
    // Image-only keys.
    switch (key) {
        case SDLK_F:      // toggle fit <-> fill-width scroll
            if (mode_ == Fit) { mode_ = FillScroll; scroll_to_image(index_); }
            else              { mode_ = Fit; fit_.fitted = false; }
            return;
        case SDLK_P:      // start the full-screen slideshow
            if (!album_.images.empty()) {
                slideshow_.start(static_cast<int>(album_.images.size()), index_);
                mode_ = Slideshow;
            }
            return;
        case SDLK_X:      // export the current image (consent modal first)
            if (!album_.images.empty())
                export_.begin(std::format("Export \"{}\"?", album_.images[index_]->name));
            return;
        default: break;
    }
    if (mode_ == FillScroll) handle_key_scroll(key);
    else                     handle_key_fit(key);
}

void ImageViewer::handle_key_fit(SDL_Keycode key)
{
    const SDL_FRect vp = viewport_rect();
    const bool zoomed = fit_.zoom > fit_.fit_zoom * 1.001f;   // zoomed past fit-to-window
    switch (key) {
        case SDLK_LEFT:  zoomed ? pan_by(PAN_STEP, 0) : set_index(-1); break;
        case SDLK_RIGHT: zoomed ? pan_by(-PAN_STEP, 0) : set_index(1); break;
        case SDLK_UP:    zoomed ? pan_by(0, PAN_STEP) : go_back(); break;
        case SDLK_DOWN:  if (zoomed) pan_by(0, -PAN_STEP); break;
        case SDLK_0:     fit_.fitted = false; break;  // reset to fit-to-window
        case SDLK_PLUS:
        case SDLK_EQUALS:
        case SDLK_KP_PLUS:  zoom_by(ZOOM_STEP, vp.x + vp.w * 0.5f, vp.y + vp.h * 0.5f); break;
        case SDLK_MINUS:
        case SDLK_KP_MINUS: zoom_by(1.0f / ZOOM_STEP, vp.x + vp.w * 0.5f, vp.y + vp.h * 0.5f); break;
        default: break;
    }
}

void ImageViewer::handle_key_scroll(SDL_Keycode key)
{
    switch (key) {
        case SDLK_DOWN:  scroll_by(SCROLL_STEP);  break;
        case SDLK_UP:    scroll_by(-SCROLL_STEP); break;
        case SDLK_RIGHT: set_index(1);            break;  // jump to next image top
        case SDLK_LEFT:  set_index(-1);           break;  // jump to prev image top
        default: break;
    }
}

void ImageViewer::handle_mouse_down(const SDL_MouseButtonEvent& b)
{
    if (b.button != SDL_BUTTON_LEFT) return;
    if (mode_ == ViewMode::Slideshow) { slideshow_.toggle_play(); return; }  // click = pause/play
    if (const int hit = strip_hit(b.x, b.y); hit >= 0) { show_image_at(hit); return; }
    // Double-click toggles fullscreen for images and video alike; checked before
    // the video branch so it takes precedence over seek-bar scrubbing.
    if (b.clicks >= 2) { win_.set_fullscreen(!win_.is_fullscreen()); return; }
    if (video_) { video_->handle_mouse_down(b.x, b.y); return; }   // seek-bar scrub
    if (mode_ != ViewMode::Fit) return;

    const SDL_FRect vp = viewport_rect();
    if (!point_in_rect(b.x, b.y, vp)) return;

    // 1.001f absorbs float rounding at the fit boundary. Edge-click only
    // navigates when not zoomed in — a zoomed click drags/pans instead.
    if (const bool zoomed = fit_.zoom > fit_.fit_zoom * 1.001f; !zoomed) {
        if (const int nav = ui::edge_nav_hit(b.x, vp.x, vp.w); nav != 0) {
            set_index(nav);
            return;
        }
    }
    fit_.dragging = true;
}

void ImageViewer::handle_wheel(const SDL_MouseWheelEvent& w)
{
    if (mode_ == ViewMode::Slideshow) return;   // no zoom/scroll while playing
    if (video_) return;                         // a playing video is fit-only (no zoom/scroll)
    if (mode_ == ViewMode::FillScroll)
        scroll_by(w.y > 0 ? -SCROLL_STEP : SCROLL_STEP);
    else
        zoom_by(w.y > 0 ? WHEEL_STEP : 1.0f / WHEEL_STEP, w.mouse_x, w.mouse_y);
}

void ImageViewer::update(double dt)
{
    if (pump_thumbs()) mark_dirty();        // thumbnail decode(s) landed — repaint
    if (full_cache_.pump()) mark_dirty();   // an off-thread decode landed — repaint

    if (mode_ == ViewMode::Slideshow) {
        slideshow_.update(dt);
        index_ = slideshow_.index();
    }

    if (video_) video_->update(dt);
    if (anim_) anim_->update(dt);
    sync_anim_for_current_index();  // reconcile anim_ when index_ changed via scroll

    // Update strip hover animation (Phase 47 Task 10): independent of the main anim_
    // playback (both may run at once). Only update when the strip is visible.
    // Note: hovered index calculation requires the strip layout to be known, which
    // is determined during render. For now, always update to handle hover state
    // properly even if the strip is off-screen; render_strip will decide whether
    // to actually paint it.
    if (const int strip_thumb = strip_hit(win_.mouse_x(), win_.mouse_y());
        strip_hover_gate_.update(strip_thumb, dt)) {
        // Dwell completed; start animation if possible
        start_strip_hover_animation(strip_thumb);
    } else if (strip_hover_gate_.active_tile() != strip_hover_anim_index_) {
        // Cursor moved off the hovered thumbnail
        strip_hover_anim_.reset();
        strip_hover_anim_index_ = -1;
    }
    if (strip_hover_anim_) {
        strip_hover_anim_->update(dt);
        // Enforce the frame count budget: if the animation has decoded more than 300 frames,
        // tear down the playback immediately to cap resource cost.
        if (anim_hover_frame_count_exceeded(strip_hover_anim_->frame_count())) {
            strip_hover_anim_.reset();
            strip_hover_anim_index_ = -1;
        }
    }

    // Single-image export runs synchronously — one image is fast (the large,
    // multi-image export lives in GalleryGrid, which backgrounds it, Phase 25).
    if (auto dest = export_.take_destination(); dest && !album_.images.empty()) {
        const std::array<const vault::IndexNode*, 1> one{album_.images[index_]};
        const ExportSummary sum =
            export_images(vault_, one, *dest, ExportConsent::Confirm);
        export_.set_status(sum.written == 1
                               ? std::format("Exported to {}", dest->string())
                               : "Export failed.");
        mark_dirty();   // export folder picker resolved — repaint the status line
    }
}

// A modal overlay (search / tag editor / export consent / quick-switch) consumes
// all input while open, in priority order. Returns true if it handled the event.
bool ImageViewer::handle_overlay_event(const SDL_Event& e)
{
    if (search_.active()) {
        if (search_.handle_event(e)) {
            Nav nav = search_.take_nav();
            if (nav.kind != NavKind::None) request(nav.kind, std::move(nav.path), nav.index);
        }
        return true;
    }
    if (tag_editor_.active()) {
        (void)tag_editor_.handle_event(e);
        return true;
    }
    if (export_.modal_active()) {
        if (e.type == SDL_EVENT_KEY_DOWN) export_.consume_key(e.key.key);
        return true;
    }
    if (quick_switch_.active()) {
        (void)quick_switch_.handle_event(e);
        if (std::string p; quick_switch_.consume_choice(p))
            request(NavKind::ToUnlock, std::move(p));   // locks current, unlocks chosen
        return true;
    }
    return false;
}

void ImageViewer::handle_event(const SDL_Event& e)
{
    using enum ViewMode;
    if (handle_overlay_event(e)) return;

    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
            // Phase 50: Shift+I opens import status
            if ((e.key.key == SDLK_I) && (e.key.mod & SDL_KMOD_SHIFT)) {
                request(NavKind::ToImportStatus);
                break;
            }
            // `/` opens search. It is a shifted key on many non-US layouts, so
            // resolve the produced character from scancode + modifiers. Not while
            // the slideshow owns the keyboard. The `` ` `` quick-switch chord is
            // layout-robust for the same reason (see is_quick_switch_key).
            if (mode_ != Slideshow && is_search_key(e.key))
                search_.open();
            else if (mode_ != Slideshow && is_quick_switch_key(e.key))
                quick_switch_.open();
            else
                handle_key(e.key.key, e.key.scancode);
            break;
        case SDL_EVENT_MOUSE_WHEEL:       handle_wheel(e.wheel);        break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: handle_mouse_down(e.button);  break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                fit_.dragging = false;
                if (video_) video_->handle_mouse_up();
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (video_)
                video_->handle_mouse_motion(e.motion.x, e.motion.y,
                                            (e.motion.state & SDL_BUTTON_LMASK) != 0);
            else if (fit_.dragging && mode_ == Fit)
                pan_by(e.motion.xrel, e.motion.yrel);
            break;
        default: break;
    }
}

std::vector<ui::HelpGroup> ImageViewer::help_groups() const
{
    std::vector<ui::HelpGroup> groups;
    if (mode_ == ViewMode::Slideshow) {
        groups.push_back({"Slideshow", {
            {"Space / Click", "Play/Pause"},
            {"[ / ]", "Dwell time -/+"},
            {"Esc", "Exit slideshow"},
        }});
        return groups;
    }
    const bool is_video = !album_.images.empty() && item_is_video(album_.images, index_);
    const bool is_animated = !album_.images.empty() && item_is_animated(album_.images, index_);
    if (is_video) {
        groups.push_back({"Video playback", {
            {"Space", "Play/Pause"}, {"J / L", "Seek -/+5s"}, {", / .", "Step one frame"},
            {"- / +", "Volume"}, {"M", "Mute"}, {"R", "Toggle loop"},
            {"Left/Right", "Prev/Next item"},
        }});
    } else if (is_animated) {
        groups.push_back({"Animation playback", {
            {"Space", "Play/Pause"},
            {"Left/Right", "Prev/Next item"},
        }});
    } else if (mode_ == ViewMode::FillScroll) {
        groups.push_back({"Scroll view", {
            {"Wheel", "Scroll"}, {"Left/Right", "Prev/Next item"},
            {"F", "Switch to fit view"}, {"P", "Start slideshow"},
        }});
    } else {
        groups.push_back({"Fit view", {
            {"Left/Right", "Prev/Next item"}, {"Wheel / + / -", "Zoom"},
            {"F", "Switch to fill-scroll view"}, {"P", "Start slideshow"},
        }});
    }
    groups.push_back({"View & session", {
        {"F11 / double-click", "Toggle fullscreen"},
        {"T", "Toggle thumbnail strip side"},
        {"B", "Favorite"}, {"G", "Edit tags"}, {"X", "Export"},
        {"Shift+I", "Import status"},
        {"U", "Keep unlocked for session"}, {"Esc", "Back"},
    }});
    return groups;
}

// --- Rendering -------------------------------------------------------------

void ImageViewer::render_fit(gfx::Renderer& r, const SDL_FRect& vp)
{
    FullTex* ft = full_cache_.acquire(*album_.images[index_]);
    const std::array<uint64_t, 1> keep{album_.images[index_]->meta.data_offset};
    full_cache_.evict_except(keep);

    if (!ft) {
        if (!full_cache_.error().empty())
            r.draw_text(font_, vp.x + 20, vp.y + vp.h * 0.5f, full_cache_.error(),
                        gfx::theme::DANGER);
        return;
    }
    fit_.img_size = Vec2{ft->w, ft->h};

    fit_.fit_zoom = clamp_zoom(fit_zoom(fit_.img_size.x, fit_.img_size.y, vp.w, vp.h));
    if (!fit_.fitted) { fit_.zoom = fit_.fit_zoom; fit_.pan = Vec2{}; fit_.fitted = true; }

    const float sw = fit_.img_size.x * fit_.zoom;
    const float sh = fit_.img_size.y * fit_.zoom;
    fit_.pan = clamp_pan(fit_.pan, sw, sh, vp.w, vp.h);
    const float dx = vp.x + vp.w * 0.5f + fit_.pan.x - sw * 0.5f;
    const float dy = vp.y + vp.h * 0.5f + fit_.pan.y - sh * 0.5f;

    const SDL_Rect clip{static_cast<int>(vp.x), static_cast<int>(vp.y),
                        static_cast<int>(vp.w), static_cast<int>(vp.h)};
    SDL_SetRenderClipRect(r.sdl(), &clip);
    const SDL_FRect dest{dx, dy, sw, sh};
    if (anim_index_ == index_ && anim_ != nullptr && anim_->valid()) {
        anim_->render(r, dest);
    } else {
        r.draw_image(ft->tex, dest);
    }
    SDL_SetRenderClipRect(r.sdl(), nullptr);
}

void ImageViewer::render_scroll(gfx::Renderer& r, const SDL_FRect& vp)
{
    const ScrollModel& m = build_scroll_model();
    scroll_y_ = m.clamp_scroll(scroll_y_);
    index_    = m.active_index(scroll_y_);

    const auto [first, last] = m.visible_range(scroll_y_);
    if (first > last) return;

    // Keep the visible images plus one neighbour each side decoded; evict rest.
    const int lo = std::max(0, first - 1);
    const int hi = std::min(m.count() - 1, last + 1);
    scratch_.keep.clear();
    scratch_.keep.reserve(static_cast<size_t>(hi - lo + 1));
    for (int i = lo; i <= hi; ++i) scratch_.keep.push_back(album_.images[i]->meta.data_offset);
    full_cache_.evict_except(scratch_.keep);

    const SDL_Rect clip{static_cast<int>(vp.x), static_cast<int>(vp.y),
                        static_cast<int>(vp.w), static_cast<int>(vp.h)};
    SDL_SetRenderClipRect(r.sdl(), &clip);
    for (int i = first; i <= last; ++i) {
        const float top = vp.y + m.image_top(i) - scroll_y_;
        const float h   = scaled_height(*album_.images[i], vp.w);
        const SDL_FRect dest{vp.x, top, vp.w, h};
        if (i == index_ && anim_index_ == index_ && anim_ != nullptr && anim_->valid()) {
            anim_->render(r, dest);
        } else if (FullTex* ft = full_cache_.acquire(*album_.images[i])) {
            r.draw_image(ft->tex, dest);
        } else {
            r.draw_rect(dest, gfx::theme::SURFACE);
        }
    }
    SDL_SetRenderClipRect(r.sdl(), nullptr);
}

void ImageViewer::render_strip(gfx::Renderer& r)
{
    using namespace gfx::theme;
    const SDL_FRect strip = strip_rect();
    r.draw_rect(strip, gfx::theme::STRIP_BG);

    const float thumb = thumb_size();
    const bool  vertical = (strip_side_ == StripSide::Left);
    const auto  count = static_cast<int>(album_.images.size());
    const float extent = vertical ? strip.h : strip.w;
    const float scroll = strip_scroll_centered(index_, count, thumb, STRIP_GAP, extent);

    // Request textures ONLY for cells near the visible window. Requesting the
    // whole album enqueues every thumbnail in the album as a background
    // fetch+decode job (Phase 58), which grinds the disk for minutes on a big
    // cold vault and starves the render thread's video demux reads — and an
    // album larger than the texture budget re-evicts and re-fetches forever.
    // Off-window cells draw as placeholders until they scroll near.
    scratch_.thumbs.clear();
    scratch_.thumbs.assign(album_.images.size(), nullptr);
    scratch_.strip_keys.clear();
    const auto [first, last] =
        strip_visible_range(scroll, extent, thumb, STRIP_GAP, count, STRIP_PREFETCH_CELLS);
    for (int i = first; i <= last; ++i) {
        scratch_.thumbs[static_cast<size_t>(i)] = thumb_texture(*album_.images[i]);
        if (const ui::ThumbKey k = ui::thumb_key_for(*album_.images[i]); k.present)
            scratch_.strip_keys.push_back(k.key);
    }
    // Drop queued fetches that fell out of the window (a running one finishes);
    // they re-enqueue if their cell scrolls near again.
    thumbs_.worker.retain(scratch_.strip_keys);
    r.draw_thumbnail_strip(scratch_.thumbs, strip,
                           gfx::ThumbnailStrip{.size = thumb, .gap = STRIP_GAP,
                                               .scroll = scroll, .selected = index_,
                                               .highlight = gfx::theme::ACCENT,
                                               .vertical = vertical});

    // Draw hover animation if active on a thumbnail, and draw animated badges on
    // tiles. Bounded to the same near-visible window: off-window cells draw no
    // thumbnail, so hover/badges there would be invisible draw calls anyway.
    for (int i = first; i <= last; ++i) {
        // Get the thumbnail's rect from the single source of truth (strip_layout).
        const SDL_FRect thumb_rect = strip_cell_rect(i, strip, thumb,
                                                     STRIP_GAP, scroll, vertical);

        // Render hover animation if active on this thumbnail.
        if (strip_hover_anim_ && strip_hover_anim_->valid() && i == strip_hover_anim_index_) {
            strip_hover_anim_->render(r, thumb_rect);
        }

        // Draw animated badge.
        if (tile_shows_animated_badge(*album_.images[static_cast<size_t>(i)])) {
            // Use 12x12 badge size and y_offset of 6 to match the original positioning.
            draw_animated_badge(r, font_, thumb_rect, 12.0f, 0.0f, 6.0f);
        }
    }
}

void ImageViewer::render_hud(gfx::Renderer& r)
{
    using enum ViewMode;

    // The header and footer are OPAQUE reserved bands, not scrims: the media is
    // fit into chrome.content and never extends under them, so nothing shows
    // through to wash the text out and no part of the picture is hidden. (This
    // replaced a translucent black scrim painted straight over the image, which
    // was both hard to read on bright content and covered the top of every
    // photo, plus a bottom status line with no backing at all.)
    const ChromeBands chrome = viewer_chrome(*this);

    // Fullscreen collapses the header to nothing for an edge-to-edge picture;
    // with no band to sit in, the HUD text is dropped rather than painted onto
    // the image.
    if (chrome.header.h > 0.0f) {
        const SDL_FRect& hb = chrome.header;
        draw_chrome_band(r, hb, gfx::theme::STRIP_BG, /*rule_at_bottom*/ true);

        if (!album_.images.empty()) {
            const vault::IndexNode& cur = *album_.images[index_];
            // A leading "* " marks the current item as favorited (the baked font is
            // ASCII-only, so an asterisk stands in for a star glyph).
            const char* star = cur.favorite ? "* " : "";
            std::string hud;
            if (item_is_video(album_.images, index_))
                hud = std::format("{}{}   {}/{}   {}   {}", star, cur.name, index_ + 1,
                                  album_.images.size(), video_type_label(cur.vmeta.codec),
                                  format_duration(cur.vmeta.duration_us));
            else if (mode_ == FillScroll)
                hud = std::format("{}{}   {}/{}   fill", star, cur.name, index_ + 1,
                                  album_.images.size());
            else
                hud = std::format("{}{}   {}/{}   {}%", star, cur.name, index_ + 1,
                                  album_.images.size(),
                                  static_cast<int>(fit_.zoom * 100.0f + 0.5f));
            r.draw_text(font_, hb.x + 16, hb.y + 12, fit_text(font_, hud, hb.w - 32),
                        cur.favorite ? gfx::theme::FAVORITE : gfx::theme::TEXT);
        }

        r.draw_text(font_, hb.x + 16, hb.y + 44, "[F1] Help", gfx::theme::TEXT_FAINT);
    }

    // Footer band. Windowed it is always reserved (so the image never resizes
    // when a status comes and goes); fullscreen forces it in only while there is
    // something to say — viewer_chrome() and this share viewer_footer_text(), so
    // the reserved height and the drawn line can never disagree.
    if (chrome.footer.h <= 0.0f) return;
    const SDL_FRect& fb = chrome.footer;
    draw_chrome_band(r, fb, gfx::theme::STRIP_BG, /*rule_at_bottom*/ false);

    if (const std::string msg = viewer_footer_text(*this); !msg.empty()) {
        const gfx::Color c = export_.status().empty() ? gfx::theme::TEXT_DIM : gfx::theme::OK;
        r.draw_text(font_, fb.x + 16, font_.text_top_for_center(fb.y + fb.h * 0.5f),
                    fit_text(font_, msg, fb.w - 32), c);
    }
}

void ImageViewer::render(gfx::Renderer& r)
{
    // Slideshow is full-screen (no thumbnail strip): it owns the whole window.
    if (mode_ == ViewMode::Slideshow) {
        slideshow_.render(r, font_, full_cache_, album_.images,
                          static_cast<float>(win_.width()),
                          static_cast<float>(win_.height()));
        return;
    }

    // `vp` is the media area only — the reserved chrome bands are NOT part of it,
    // so the image/video is fit strictly between them and no band ever covers
    // picture content. The backdrop still fills the bands' rows so a resize can't
    // leave a stale gap before render_hud() paints over them.
    const ChromeBands chrome = viewer_chrome(*this);
    const SDL_FRect   vp     = chrome.content;
    r.draw_rect({chrome.header.x, chrome.header.y, chrome.header.w,
                 chrome.header.h + chrome.content.h + chrome.footer.h},
                gfx::theme::IMG_BG);

    if (video_) {   // Fit mode + current item is a video
        if (video_->valid()) {
            video_->render(r, font_, vp);
        } else if (SDL_Texture* tex = thumb_texture(*album_.images[index_])) {
            float tw = 0.0f;          // poster fallback (no decoder / non-AV build)
            float th = 0.0f;
            SDL_GetTextureSize(tex, &tw, &th);
            const float s = fit_zoom(tw, th, vp.w, vp.h);
            r.draw_image(tex, {vp.x + (vp.w - tw * s) * 0.5f,
                               vp.y + (vp.h - th * s) * 0.5f, tw * s, th * s});
        }
    } else if (mode_ == ViewMode::FillScroll) {
        render_scroll(r, vp);
    } else {
        render_fit(r, vp);
    }

    render_hud(r);
    if (!win_.is_fullscreen()) render_strip(r);   // Phase 45 Part 4

    export_.render(r, font_, static_cast<float>(win_.width()),
                   static_cast<float>(win_.height()));
    tag_editor_.render(r, font_, static_cast<float>(win_.width()),
                       static_cast<float>(win_.height()));
    search_.render(r, font_, static_cast<float>(win_.width()),
                   static_cast<float>(win_.height()));
    quick_switch_.render(r, font_, static_cast<float>(win_.width()),
                         static_cast<float>(win_.height()));
}

StripSide current_strip_side(const ImageViewer& v) { return v.strip_side_; }

void capture_video_resume(const ImageViewer& v, GallerySessionState& session)
{
    if (v.video_ && v.video_->valid() && v.index_ >= 0 &&
        v.index_ < static_cast<int>(v.album_.paths.size())) {
        session.last_media_path      = v.album_.paths[static_cast<size_t>(v.index_)];
        session.video_resume_seconds = v.video_->position();
    } else {
        session.last_media_path.clear();
        session.video_resume_seconds = 0.0;
    }
}

void apply_video_resume(ImageViewer& v, const GallerySessionState& session)
{
    if (session.video_resume_seconds <= 0.0 || !v.video_ || !v.video_->valid()) return;
    if (v.index_ < 0 || v.index_ >= static_cast<int>(v.album_.paths.size())) return;
    if (v.album_.paths[static_cast<size_t>(v.index_)] != session.last_media_path) return;
    v.video_->seek(session.video_resume_seconds);
}

} // namespace ui
