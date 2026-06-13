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
#include "image/decode.h"
#include "ui/widgets.h"
#include "vault/index.h"
#include "vault/vault.h"

namespace ui {

namespace {
constexpr float PAN_STEP    = 64.0f;   // arrow-key pan distance (px)
constexpr float ZOOM_STEP   = 1.25f;   // keyboard +/- zoom factor
constexpr float WHEEL_STEP  = 1.10f;   // per-notch wheel zoom factor (fit mode)
constexpr float SCROLL_STEP = 96.0f;   // arrow-key / wheel scroll distance (px)
} // namespace

ImageViewer::ImageViewer(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                         gfx::TextureCache& cache, std::string gallery_path,
                         int start_index)
    : win_(win), font_(font), vault_(vault), cache_(cache),
      gallery_path_(std::move(gallery_path)), index_(start_index)
{
}

ImageViewer::~ImageViewer()
{
    evict_full_except({});   // destroy all cached textures
}

void ImageViewer::on_enter()
{
    // Snapshot the leaf gallery's images. A leaf gallery holds only images, but
    // filter defensively so a stray sub-gallery can never be "viewed".
    images_.clear();
    for (const vault::IndexNode* n : vault_.list(gallery_path_))
        if (n->is_image()) images_.push_back(n);

    if (images_.empty()) { back_to_gallery(); return; }
    index_  = std::clamp(index_, 0, static_cast<int>(images_.size()) - 1);
    fitted_ = false;
}

void ImageViewer::on_exit()
{
    evict_full_except({});   // destroy all cached textures
}

// --- Geometry --------------------------------------------------------------

float ImageViewer::thumb_size() const
{
    return strip_thumb_size(static_cast<float>(win_.height()));
}

SDL_FRect ImageViewer::viewport_rect() const
{
    return viewport_rect_for(strip_side_, static_cast<float>(win_.width()),
                             static_cast<float>(win_.height()), thumb_size());
}

SDL_FRect ImageViewer::strip_rect() const
{
    return strip_rect_for(strip_side_, static_cast<float>(win_.width()),
                          static_cast<float>(win_.height()), thumb_size());
}

// --- Mode / layout toggles -------------------------------------------------

// --- Fit-mode view state ---------------------------------------------------

bool ImageViewer::is_zoomed() const noexcept
{
    return zoom_ > fit_zoom_ * 1.001f;
}

void ImageViewer::show_image_at(int idx)
{
    if (images_.empty()) return;
    index_    = std::clamp(idx, 0, static_cast<int>(images_.size()) - 1);
    fitted_   = false;
    dragging_ = false;
    error_.clear();
    if (mode_ == ViewMode::FillScroll) scroll_to_image(index_);
}

void ImageViewer::set_index(int delta)
{
    show_image_at(wrap_index(index_, delta, static_cast<int>(images_.size())));
}

void ImageViewer::back_to_gallery()
{
    request(NavKind::ToGallery, gallery_path_, index_);
}

void ImageViewer::zoom_by(float factor, float cx, float cy)
{
    const SDL_FRect vp = viewport_rect();
    const ZoomResult z = zoom_at(Vec2{img_w_, img_h_}, zoom_, pan_, factor,
                                 Vec2{cx - vp.x, cy - vp.y}, Vec2{vp.w, vp.h});
    zoom_ = z.zoom;
    pan_  = z.pan;
}

void ImageViewer::pan_by(float dx, float dy)
{
    const SDL_FRect vp = viewport_rect();
    pan_ = clamp_pan(Vec2{pan_.x + dx, pan_.y + dy}, img_w_ * zoom_, img_h_ * zoom_,
                     vp.w, vp.h);
}

// --- FillScroll-mode helpers ----------------------------------------------

float ImageViewer::scaled_height(const vault::IndexNode& n, float vp_w) const
{
    const float w = n.meta.width  ? static_cast<float>(n.meta.width)  : 1.0f;
    const float h = n.meta.height ? static_cast<float>(n.meta.height) : 1.0f;
    return vp_w * (h / w);
}

ScrollModel ImageViewer::build_scroll_model() const
{
    const SDL_FRect vp = viewport_rect();
    std::vector<float> heights;
    heights.reserve(images_.size());
    for (const vault::IndexNode* n : images_) heights.push_back(scaled_height(*n, vp.w));
    return ScrollModel(std::move(heights), vp.h);
}

void ImageViewer::scroll_to_image(int idx)
{
    const ScrollModel m = build_scroll_model();
    scroll_y_ = m.clamp_scroll(m.image_top(std::clamp(idx, 0, m.count() - 1)));
}

void ImageViewer::scroll_by(float dy)
{
    const ScrollModel m = build_scroll_model();
    scroll_y_ = m.clamp_scroll(scroll_y_ + dy);
    index_    = m.active_index(scroll_y_);
}

// --- Decoded full-texture cache -------------------------------------------

ImageViewer::FullTex* ImageViewer::acquire_full(const vault::IndexNode& node)
{
    const uint64_t key = node.meta.data_offset;
    if (auto it = full_cache_.find(key); it != full_cache_.end()) return &it->second;

    crypto::SecureBytes sb;
    if (vault_.read_image(node, sb) != vault::VaultResult::Ok) {
        error_ = "Could not decrypt image.";
        return nullptr;
    }
    auto img = image::decode_from_memory(sb.as_span());
    if (!img) { error_ = "Could not decode image."; return nullptr; }

    SDL_Texture* tex = SDL_CreateTexture(win_.sdl_renderer(), SDL_PIXELFORMAT_RGB24,
                                         SDL_TEXTUREACCESS_STATIC, img->width, img->height);
    if (!tex) { error_ = "Could not upload image."; return nullptr; }
    if (!SDL_UpdateTexture(tex, nullptr, img->pixels.data(), img->width * 3)) {
        SDL_DestroyTexture(tex);
        error_ = "Could not upload image.";
        return nullptr;
    }
    // SecureBytes `sb` wipes the decrypted plaintext on scope exit; the pixels
    // now live only in the GPU texture.
    auto [it, _] = full_cache_.try_emplace(key, FullTex{tex,
                                                        static_cast<float>(img->width),
                                                        static_cast<float>(img->height)});
    return &it->second;
}

void ImageViewer::evict_full_except(std::span<const uint64_t> keep)
{
    std::erase_if(full_cache_, [keep](const auto& entry) {
        const auto& [key, ft] = entry;
        if (std::ranges::find(keep, key) != keep.end()) return false;
        SDL_DestroyTexture(ft.tex);
        return true;
    });
}

SDL_Texture* ImageViewer::thumb_texture(const vault::IndexNode& node)
{
    if (node.meta.thumb_length == 0) return nullptr;
    const uint64_t key = node.meta.data_offset;
    if (SDL_Texture* t = cache_.get(key)) return t;

    crypto::SecureBytes sb;
    if (vault_.read_thumbnail(node, sb) != vault::VaultResult::Ok) return nullptr;
    auto img = image::decode_from_memory(sb.as_span());
    if (!img) return nullptr;
    return cache_.get_or_upload(key, *img);
}

int ImageViewer::strip_hit(float mx, float my) const
{
    const SDL_FRect strip   = strip_rect();
    const float     thumb   = thumb_size();
    const bool      vertical = (strip_side_ == StripSide::Left);

    const float cross0 = vertical ? strip.x + (strip.w - thumb) * 0.5f
                                   : strip.y + (strip.h - thumb) * 0.5f;
    if (const float cross = vertical ? mx : my; cross < cross0 || cross > cross0 + thumb)
        return -1;

    const float extent = vertical ? strip.h : strip.w;
    const float scroll = strip_scroll_centered(index_, static_cast<int>(images_.size()),
                                               thumb, STRIP_GAP, extent);
    const float along  = vertical ? my : mx;
    const float origin = vertical ? strip.y : strip.x;
    return strip_hit_axis(along, origin, scroll, thumb, STRIP_GAP,
                          static_cast<int>(images_.size()));
}

// --- Input -----------------------------------------------------------------

void ImageViewer::handle_key(SDL_Keycode key)
{
    using enum StripSide;
    using enum ViewMode;
    switch (key) {
        case SDLK_T:      // toggle the strip between bottom and left
            strip_side_ = (strip_side_ == Bottom) ? Left : Bottom;
            fitted_ = false;                      // viewport changed: refit
            if (mode_ == FillScroll) scroll_to_image(index_);
            return;
        case SDLK_F:      // toggle fit <-> fill-width scroll
            if (mode_ == Fit) { mode_ = FillScroll; scroll_to_image(index_); }
            else              { mode_ = Fit; fitted_ = false; }
            return;
        case SDLK_ESCAPE: back_to_gallery(); return;
        default: break;
    }
    if (mode_ == FillScroll) handle_key_scroll(key);
    else                     handle_key_fit(key);
}

void ImageViewer::handle_key_fit(SDL_Keycode key)
{
    const SDL_FRect vp = viewport_rect();
    switch (key) {
        case SDLK_LEFT:  is_zoomed() ? pan_by(PAN_STEP, 0) : set_index(-1); break;
        case SDLK_RIGHT: is_zoomed() ? pan_by(-PAN_STEP, 0) : set_index(1); break;
        case SDLK_UP:    is_zoomed() ? pan_by(0, PAN_STEP) : back_to_gallery(); break;
        case SDLK_DOWN:  if (is_zoomed()) pan_by(0, -PAN_STEP); break;
        case SDLK_0:     fitted_ = false; break;  // reset to fit-to-window
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
    if (const int hit = strip_hit(b.x, b.y); hit >= 0) {
        show_image_at(hit);
    } else if (mode_ == ViewMode::Fit) {
        const SDL_FRect vp = viewport_rect();
        if (point_in_rect(b.x, b.y, vp)) dragging_ = true;
    }
}

void ImageViewer::handle_wheel(const SDL_MouseWheelEvent& w)
{
    if (mode_ == ViewMode::FillScroll)
        scroll_by(w.y > 0 ? -SCROLL_STEP : SCROLL_STEP);
    else
        zoom_by(w.y > 0 ? WHEEL_STEP : 1.0f / WHEEL_STEP, w.mouse_x, w.mouse_y);
}

void ImageViewer::handle_event(const SDL_Event& e)
{
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:          handle_key(e.key.key);        break;
        case SDL_EVENT_MOUSE_WHEEL:       handle_wheel(e.wheel);        break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: handle_mouse_down(e.button);  break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) dragging_ = false;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (dragging_ && mode_ == ViewMode::Fit) pan_by(e.motion.xrel, e.motion.yrel);
            break;
        default: break;
    }
}

// --- Rendering -------------------------------------------------------------

void ImageViewer::render_fit(gfx::Renderer& r, const SDL_FRect& vp)
{
    FullTex* ft = acquire_full(*images_[index_]);
    const std::array<uint64_t, 1> keep{images_[index_]->meta.data_offset};
    evict_full_except(keep);

    if (!ft) {
        if (!error_.empty())
            r.draw_text(font_, vp.x + 20, vp.y + vp.h * 0.5f, error_, gfx::theme::DANGER);
        return;
    }
    img_w_ = ft->w;
    img_h_ = ft->h;

    fit_zoom_ = clamp_zoom(fit_zoom(img_w_, img_h_, vp.w, vp.h));
    if (!fitted_) { zoom_ = fit_zoom_; pan_ = Vec2{}; fitted_ = true; }

    const float sw = img_w_ * zoom_;
    const float sh = img_h_ * zoom_;
    pan_ = clamp_pan(pan_, sw, sh, vp.w, vp.h);
    const float dx = vp.x + vp.w * 0.5f + pan_.x - sw * 0.5f;
    const float dy = vp.y + vp.h * 0.5f + pan_.y - sh * 0.5f;

    const SDL_Rect clip{static_cast<int>(vp.x), static_cast<int>(vp.y),
                        static_cast<int>(vp.w), static_cast<int>(vp.h)};
    SDL_SetRenderClipRect(r.sdl(), &clip);
    r.draw_image(ft->tex, SDL_FRect{dx, dy, sw, sh});
    SDL_SetRenderClipRect(r.sdl(), nullptr);
}

void ImageViewer::render_scroll(gfx::Renderer& r, const SDL_FRect& vp)
{
    const ScrollModel m = build_scroll_model();
    scroll_y_ = m.clamp_scroll(scroll_y_);
    index_    = m.active_index(scroll_y_);

    const auto [first, last] = m.visible_range(scroll_y_);
    if (first > last) return;

    // Keep the visible images plus one neighbour each side decoded; evict rest.
    const int lo = std::max(0, first - 1);
    const int hi = std::min(m.count() - 1, last + 1);
    std::vector<uint64_t> keep;
    keep.reserve(static_cast<size_t>(hi - lo + 1));
    for (int i = lo; i <= hi; ++i) keep.push_back(images_[i]->meta.data_offset);
    evict_full_except(keep);

    const SDL_Rect clip{static_cast<int>(vp.x), static_cast<int>(vp.y),
                        static_cast<int>(vp.w), static_cast<int>(vp.h)};
    SDL_SetRenderClipRect(r.sdl(), &clip);
    for (int i = first; i <= last; ++i) {
        const float top = vp.y + m.image_top(i) - scroll_y_;
        const float h   = scaled_height(*images_[i], vp.w);
        if (FullTex* ft = acquire_full(*images_[i]))
            r.draw_image(ft->tex, SDL_FRect{vp.x, top, vp.w, h});
        else
            r.draw_rect(SDL_FRect{vp.x, top, vp.w, h}, gfx::theme::SURFACE);
    }
    SDL_SetRenderClipRect(r.sdl(), nullptr);
}

void ImageViewer::render_strip(gfx::Renderer& r)
{
    const SDL_FRect strip = strip_rect();
    r.draw_rect(strip, gfx::theme::STRIP_BG);

    const float thumb = thumb_size();
    const bool  vertical = (strip_side_ == StripSide::Left);
    std::vector<SDL_Texture*> thumbs;
    thumbs.reserve(images_.size());
    for (const vault::IndexNode* n : images_) thumbs.push_back(thumb_texture(*n));

    const float extent = vertical ? strip.h : strip.w;
    const float scroll = strip_scroll_centered(index_, static_cast<int>(images_.size()),
                                               thumb, STRIP_GAP, extent);
    r.draw_thumbnail_strip(thumbs, strip,
                           gfx::ThumbnailStrip{.size = thumb, .gap = STRIP_GAP,
                                               .scroll = scroll, .selected = index_,
                                               .highlight = gfx::theme::ACCENT,
                                               .vertical = vertical});
}

void ImageViewer::render_hud(gfx::Renderer& r, const SDL_FRect& vp)
{
    using enum ViewMode;
    if (!images_.empty()) {
        const char* mode = (mode_ == FillScroll) ? "fill" : "fit";
        const std::string hud = (mode_ == FillScroll)
            ? std::format("{}   {}/{}   {}", images_[index_]->name, index_ + 1,
                          images_.size(), mode)
            : std::format("{}   {}/{}   {}%", images_[index_]->name, index_ + 1,
                          images_.size(), static_cast<int>(zoom_ * 100.0f + 0.5f));
        r.draw_text(font_, vp.x + 16, vp.y + 12, hud, gfx::theme::TEXT);
    }
    const char* legend = (mode_ == FillScroll)
        ? "[Wheel] Scroll   [<-/->] Prev/Next   [F] Fit   [T] Strip side   [Esc] Back"
        : "[<-/->] Prev/Next   [Wheel/+/-] Zoom   [F] Fill-scroll   [T] Strip side   [Esc] Back";
    r.draw_text(font_, vp.x + 16, vp.y + 44, legend, gfx::theme::TEXT_FAINT);
}

void ImageViewer::render(gfx::Renderer& r)
{
    const SDL_FRect vp = viewport_rect();
    r.draw_rect(vp, gfx::theme::IMG_BG);

    if (mode_ == ViewMode::FillScroll) render_scroll(r, vp);
    else                               render_fit(r, vp);

    render_hud(r, vp);
    render_strip(r);
}

} // namespace ui
