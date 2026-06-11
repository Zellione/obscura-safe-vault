#include "ui/image_viewer.h"

#include <algorithm>
#include <format>

#include "crypto/secure_mem.h"
#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/texture_cache.h"
#include "gfx/window.h"
#include "image/decode.h"
#include "vault/index.h"
#include "vault/vault.h"

namespace ui {

namespace {
constexpr float VIEWPORT_FRACTION = 0.75f;  // top share of the window for the image
constexpr float STRIP_MARGIN      = 16.0f;  // padding above/below thumbnails
constexpr float STRIP_GAP         = 10.0f;  // gap between thumbnails
constexpr float PAN_STEP          = 64.0f;  // arrow-key pan distance (px)
constexpr float ZOOM_STEP         = 1.25f;  // keyboard +/- zoom factor
constexpr float WHEEL_STEP        = 1.10f;  // per-notch wheel zoom factor

// Thumbnail side for a strip of the given height.
float strip_thumb(const SDL_FRect& strip) noexcept
{
    return std::max(8.0f, strip.h - 2.0f * STRIP_MARGIN);
}
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
    if (full_tex_) SDL_DestroyTexture(full_tex_);
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
    if (full_tex_) { SDL_DestroyTexture(full_tex_); full_tex_ = nullptr; }
    full_key_ = 0;
}

SDL_FRect ImageViewer::viewport_rect() const
{
    const auto W = static_cast<float>(win_.width());
    const auto H = static_cast<float>(win_.height());
    return SDL_FRect{0.0f, 0.0f, W, H * VIEWPORT_FRACTION};
}

SDL_FRect ImageViewer::strip_rect() const
{
    const auto W = static_cast<float>(win_.width());
    const auto H = static_cast<float>(win_.height());
    const float top = H * VIEWPORT_FRACTION;
    return SDL_FRect{0.0f, top, W, H - top};
}

bool ImageViewer::is_zoomed() const noexcept
{
    return zoom_ > fit_zoom_ * 1.001f;
}

void ImageViewer::show_image_at(int idx)
{
    if (images_.empty()) return;
    index_    = std::clamp(idx, 0, static_cast<int>(images_.size()) - 1);
    fitted_   = false;   // force fit-to-window for the newly shown image
    dragging_ = false;
    error_.clear();
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
                                 Vec2{cx, cy}, Vec2{vp.w, vp.h});
    zoom_ = z.zoom;
    pan_  = z.pan;
}

void ImageViewer::pan_by(float dx, float dy)
{
    const SDL_FRect vp = viewport_rect();
    pan_ = clamp_pan(Vec2{pan_.x + dx, pan_.y + dy}, img_w_ * zoom_, img_h_ * zoom_,
                     vp.w, vp.h);
}

SDL_Texture* ImageViewer::full_texture()
{
    if (images_.empty()) return nullptr;
    const vault::IndexNode& node = *images_[index_];
    if (full_tex_ && full_key_ == node.meta.data_offset) return full_tex_;

    if (full_tex_) { SDL_DestroyTexture(full_tex_); full_tex_ = nullptr; }

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

    full_tex_ = tex;
    full_key_ = node.meta.data_offset;
    img_w_    = static_cast<float>(img->width);
    img_h_    = static_cast<float>(img->height);
    return full_tex_;
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
    const SDL_FRect strip = strip_rect();
    const float thumb = strip_thumb(strip);
    if (const float top = strip.y + (strip.h - thumb) * 0.5f;
        my < top || my > top + thumb)
        return -1;

    const float scroll = strip_scroll_centered(index_, static_cast<int>(images_.size()),
                                                thumb, STRIP_GAP, strip.w);
    for (int i = 0; i < static_cast<int>(images_.size()); ++i) {
        const float cell_x = strip.x - scroll + static_cast<float>(i) * (thumb + STRIP_GAP);
        if (mx >= cell_x && mx <= cell_x + thumb) return i;
    }
    return -1;
}

void ImageViewer::handle_key(SDL_Keycode key)
{
    const SDL_FRect vp = viewport_rect();
    switch (key) {
        case SDLK_LEFT:  is_zoomed() ? pan_by(PAN_STEP, 0) : set_index(-1); break;
        case SDLK_RIGHT: is_zoomed() ? pan_by(-PAN_STEP, 0) : set_index(1); break;
        case SDLK_UP:    is_zoomed() ? pan_by(0, PAN_STEP) : back_to_gallery(); break;
        case SDLK_DOWN:  if (is_zoomed()) pan_by(0, -PAN_STEP); break;
        case SDLK_ESCAPE: back_to_gallery(); break;
        case SDLK_0:      fitted_ = false; break;  // reset to fit-to-window
        case SDLK_PLUS:
        case SDLK_EQUALS:
        case SDLK_KP_PLUS:  zoom_by(ZOOM_STEP, vp.w * 0.5f, vp.h * 0.5f); break;
        case SDLK_MINUS:
        case SDLK_KP_MINUS: zoom_by(1.0f / ZOOM_STEP, vp.w * 0.5f, vp.h * 0.5f); break;
        default: break;
    }
}

void ImageViewer::handle_mouse_down(const SDL_MouseButtonEvent& b)
{
    if (b.button != SDL_BUTTON_LEFT) return;
    if (const int hit = strip_hit(b.x, b.y); hit >= 0)
        show_image_at(hit);
    else if (b.y < viewport_rect().h)
        dragging_ = true;
}

void ImageViewer::handle_event(const SDL_Event& e)
{
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN: handle_key(e.key.key); break;
        case SDL_EVENT_MOUSE_WHEEL:
            zoom_by(e.wheel.y > 0 ? WHEEL_STEP : 1.0f / WHEEL_STEP,
                    e.wheel.mouse_x, e.wheel.mouse_y);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: handle_mouse_down(e.button); break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) dragging_ = false;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (dragging_) pan_by(e.motion.xrel, e.motion.yrel);
            break;
        default: break;
    }
}

void ImageViewer::render(gfx::Renderer& r)
{
    const SDL_FRect vp    = viewport_rect();
    const SDL_FRect strip = strip_rect();

    SDL_Texture* tex = full_texture();

    // Keep fit_zoom_ tracking the live viewport so is_zoomed() stays meaningful
    // across resizes; (re)fit only when a new image hasn't been fitted yet.
    if (tex) {
        fit_zoom_ = clamp_zoom(fit_zoom(img_w_, img_h_, vp.w, vp.h));
        if (!fitted_) { zoom_ = fit_zoom_; pan_ = Vec2{}; fitted_ = true; }
    }

    // --- Image area --------------------------------------------------------
    r.draw_rect(vp, gfx::Color{12, 12, 16, 255});
    if (tex) {
        const float sw = img_w_ * zoom_;
        const float sh = img_h_ * zoom_;
        pan_ = clamp_pan(pan_, sw, sh, vp.w, vp.h);  // keep on-screen after resize
        const float dx = vp.x + vp.w * 0.5f + pan_.x - sw * 0.5f;
        const float dy = vp.y + vp.h * 0.5f + pan_.y - sh * 0.5f;

        const SDL_Rect clip{static_cast<int>(vp.x), static_cast<int>(vp.y),
                            static_cast<int>(vp.w), static_cast<int>(vp.h)};
        SDL_SetRenderClipRect(r.sdl(), &clip);
        r.draw_image(tex, SDL_FRect{dx, dy, sw, sh});
        SDL_SetRenderClipRect(r.sdl(), nullptr);
    } else if (!error_.empty()) {
        r.draw_text(font_, vp.x + 20, vp.y + vp.h * 0.5f, error_,
                    gfx::Color{230, 120, 120, 255});
    }

    // --- HUD ---------------------------------------------------------------
    if (!images_.empty()) {
        const std::string hud = std::format("{}   {}/{}   {}%", images_[index_]->name,
                                             index_ + 1, images_.size(),
                                             static_cast<int>(zoom_ * 100.0f + 0.5f));
        r.draw_text(font_, vp.x + 16, vp.y + 12, hud, gfx::Color{220, 220, 230, 255});
    }
    r.draw_text(font_, vp.x + 16, vp.y + 44,
                "[<-/->] Prev/Next   [Wheel/+/-] Zoom   [Drag] Pan   [Esc] Back",
                gfx::Color{120, 120, 130, 255});

    // --- Thumbnail strip ---------------------------------------------------
    r.draw_rect(strip, gfx::Color{24, 24, 30, 255});
    const float thumb = strip_thumb(strip);
    std::vector<SDL_Texture*> thumbs;
    thumbs.reserve(images_.size());
    for (const vault::IndexNode* n : images_) thumbs.push_back(thumb_texture(*n));

    const float scroll = strip_scroll_centered(index_, static_cast<int>(images_.size()),
                                               thumb, STRIP_GAP, strip.w);
    r.draw_thumbnail_strip(thumbs, strip, thumb, STRIP_GAP, scroll, index_,
                           gfx::Color{180, 140, 240, 255});
}

} // namespace ui
