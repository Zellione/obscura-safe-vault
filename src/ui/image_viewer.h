#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "ui/screen.h"
#include "ui/viewer_model.h"

namespace gfx { class Window; class FontAtlas; class Renderer; class TextureCache; }
namespace vault { class Vault; struct IndexNode; }

namespace ui {

// Full-window image viewer: a big zoom/pan image area (top ~75%) over a
// horizontal thumbnail strip (bottom ~25%) that auto-scrolls to the current
// image. Opened from GalleryGrid on an image tile; Up/Esc returns to the grid.
//
// The decrypted full image lives only in a transient mlock'd SecureBytes while
// decoding (invariant #1); the resulting GPU texture is owned here and rebuilt
// whenever the current image changes. Thumbnails share the App-wide TextureCache
// with the gallery grid (keyed by data_offset).
class ImageViewer : public Screen {
public:
    ImageViewer(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                gfx::TextureCache& cache, std::string gallery_path, int start_index);
    ~ImageViewer() override;

    ImageViewer(const ImageViewer&)            = delete;
    ImageViewer& operator=(const ImageViewer&) = delete;

    void on_enter() override;
    void on_exit() override;
    void handle_event(const SDL_Event& e) override;
    void render(gfx::Renderer& r) override;

private:
    void handle_key(SDL_Keycode key);
    void handle_mouse_down(const SDL_MouseButtonEvent& b);

    [[nodiscard]] SDL_FRect viewport_rect() const;  // top ~75%
    [[nodiscard]] SDL_FRect strip_rect() const;     // bottom ~25%

    void show_image_at(int idx);                    // absolute, clamped, refit
    void set_index(int delta);                      // wrap, reset view, rebuild
    void back_to_gallery();
    void zoom_by(float factor, float cx, float cy); // centred on (cx, cy)
    void pan_by(float dx, float dy);
    [[nodiscard]] bool is_zoomed() const noexcept;  // zoomed past fit-to-window

    SDL_Texture* full_texture();                            // lazy, owned
    SDL_Texture* thumb_texture(const vault::IndexNode& node); // shared cache
    [[nodiscard]] int strip_hit(float mx, float my) const;  // thumbnail under cursor, or -1

    gfx::Window&       win_;
    gfx::FontAtlas&    font_;
    vault::Vault&      vault_;
    gfx::TextureCache& cache_;
    std::string        gallery_path_;
    std::vector<const vault::IndexNode*> images_;
    int   index_ = 0;

    // View state for the current image.
    float zoom_     = 1.0f;
    float fit_zoom_ = 1.0f;   // fit-to-window scale, recomputed on (re)fit
    Vec2  pan_;
    bool  fitted_   = false;  // false until the current image has been fit-to-window
    bool  dragging_ = false;

    // Owned full-resolution texture for the current image.
    SDL_Texture* full_tex_ = nullptr;
    uint64_t     full_key_ = 0;     // data_offset the full_tex_ was built from
    float        img_w_    = 0.0f;
    float        img_h_    = 0.0f;

    std::string error_;
};

} // namespace ui
