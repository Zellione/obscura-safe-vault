#pragma once

#include <SDL3/SDL.h>

#include <optional>
#include <string>
#include <vector>

#include "image/decode_worker.h"
#include "ui/export_ui.h"
#include "ui/full_tex_cache.h"
#include "ui/screen.h"
#include "ui/scroll_model.h"
#include "ui/search_overlay.h"
#include "ui/slideshow_view.h"
#include "ui/strip_layout.h"
#include "ui/tag_editor.h"
#include "ui/viewer_model.h"

namespace gfx { class Window; class FontAtlas; class Renderer; class TextureCache; }
namespace vault { class Vault; struct IndexNode; }
namespace platform { class FolderDialog; }

namespace ui {

// Full-window image viewer with three view modes and a movable thumbnail strip:
//
//   * Fit (default): a big zoom/pan image area with the thumbnail strip beside
//     it. Wheel zooms, drag/arrows pan.
//   * FillScroll: the image is scaled to fill the viewport width and the wheel
//     scrolls vertically, flowing continuously into the next image across the
//     whole leaf gallery; the active thumbnail tracks the viewport centre.
//   * Slideshow: a full-screen auto-advancing show with a cross-fade and a live
//     dwell time (the whole concern lives in SlideshowView; `P` starts it,
//     `Esc`/`Up` returns here at the current image).
//
// The strip sits at the Bottom (horizontal) or Left (vertical); `T` toggles it,
// `F` toggles between Fit and FillScroll. The choices persist while the viewer
// is open.
//
// Decrypted image bytes live only in a transient mlock'd SecureBytes during
// decode (invariant #1); the resulting GPU textures are owned by FullTexCache.
// Fit mode keeps just the current image decoded; FillScroll keeps the on-screen
// images plus their immediate neighbours (a small bounded set), evicting the rest.
class ImageViewer : public Screen {
public:
    ImageViewer(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                gfx::TextureCache& cache, platform::FolderDialog& folder_dlg,
                std::string gallery_path, int start_index);
    ~ImageViewer() override = default;

    ImageViewer(const ImageViewer&)            = delete;
    ImageViewer& operator=(const ImageViewer&) = delete;

    void on_enter() override;
    void on_exit() override;
    void handle_event(const SDL_Event& e) override;
    void update(double dt) override;
    void render(gfx::Renderer& r) override;

    // Only the slideshow cross-fade/auto-advance animates; Fit and FillScroll are
    // static between inputs, so the app loop can idle in those modes.
    [[nodiscard]] bool animating() const override
    {
        return mode_ == ViewMode::Slideshow && slideshow_.animating();
    }

private:
    enum class ViewMode { Fit, FillScroll, Slideshow };

    void handle_key(SDL_Keycode key);
    void handle_key_fit(SDL_Keycode key);
    void handle_key_scroll(SDL_Keycode key);
    void handle_mouse_down(const SDL_MouseButtonEvent& b);
    void handle_wheel(const SDL_MouseWheelEvent& w);

    [[nodiscard]] float     thumb_size() const;
    [[nodiscard]] SDL_FRect viewport_rect() const;
    [[nodiscard]] SDL_FRect strip_rect() const;

    void show_image_at(int idx);                    // absolute, clamped, refit
    void set_index(int delta);                      // wrap, reset view, rebuild
    void back_to_gallery();
    void zoom_by(float factor, float cx, float cy); // centred on (cx, cy)
    void pan_by(float dx, float dy);
    [[nodiscard]] bool is_zoomed() const noexcept;  // zoomed past fit-to-window

    // Fill-scroll helpers. The model is cached and rebuilt only when the
    // viewport size changes (the image list is fixed for the session), since
    // render/scroll query it several times per frame.
    [[nodiscard]] const ScrollModel& build_scroll_model();
    [[nodiscard]] float       scaled_height(const vault::IndexNode& n, float vp_w) const;
    void scroll_to_image(int idx);                  // place image `idx` at the top
    void scroll_by(float dy);

    SDL_Texture* thumb_texture(const vault::IndexNode& node);  // shared cache
    [[nodiscard]] int strip_hit(float mx, float my) const;     // thumb under cursor, or -1
    void render_fit(gfx::Renderer& r, const SDL_FRect& vp);
    void render_scroll(gfx::Renderer& r, const SDL_FRect& vp);
    void render_strip(gfx::Renderer& r);
    void render_hud(gfx::Renderer& r, const SDL_FRect& vp);

    gfx::Window&            win_;
    gfx::FontAtlas&         font_;
    vault::Vault&           vault_;
    gfx::TextureCache&      cache_;
    ExportUi                export_;
    TagEditor               tag_editor_;
    SearchOverlay           search_;
    std::string             gallery_path_;
    std::vector<const vault::IndexNode*> images_;
    int   index_ = 0;

    // Persistent layout/mode choices (reset on construction, i.e. per viewer).
    StripSide strip_side_ = StripSide::Bottom;
    ViewMode  mode_       = ViewMode::Fit;

    // Fit-mode view state for the current image (grouped to keep the field count
    // in check and to document the cluster as one cohesive unit).
    struct FitState {
        float zoom     = 1.0f;
        float fit_zoom = 1.0f;
        Vec2  pan;
        Vec2  img_size;             // natural pixel size of the current image
        bool  fitted   = false;
        bool  dragging = false;
    };
    FitState fit_;

    // FillScroll-mode state.
    float scroll_y_ = 0.0f;

    // Cached fill-scroll model + the viewport size it was built for; rebuilt
    // lazily by build_scroll_model() when that size changes. Avoids reallocating
    // the height/prefix-sum vectors every frame.
    struct ScrollCache {
        std::optional<ScrollModel> model;
        float                      w = -1.0f;
        float                      h = -1.0f;
        size_t                     n = 0;
    };
    ScrollCache scroll_cache_;

    // Reused render-path scratch buffers (cleared, not reallocated, each frame).
    struct RenderScratch {
        std::vector<uint64_t>     keep;     // evict keep-list
        std::vector<SDL_Texture*> thumbs;   // strip textures
    };
    RenderScratch scratch_;

    // Full-screen slideshow (active while mode_ == Slideshow).
    SlideshowView slideshow_;

    // Off-thread decoder for full-res images, scoped to this viewer so its
    // pending decodes are dropped when the viewer closes. Declared before
    // full_cache_ so the cache can hold a pointer to it. (Each screen owns its
    // own worker: thumbnail and full-image decodes key on the same data_offset,
    // so a shared worker would risk cross-contaminating the two caches.)
    image::DecodeWorker decode_worker_{image::decode_wake_event()};

    // Bounded set of decoded full-res textures (shared with the slideshow).
    FullTexCache full_cache_;
};

} // namespace ui
