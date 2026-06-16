#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "ui/slideshow_model.h"

namespace gfx { class Renderer; class FontAtlas; }
namespace vault { struct IndexNode; }

namespace ui {

class FullTexCache;

// Full-screen auto-advancing slideshow over a leaf gallery (Phase 11). Owns the
// pure SlideshowModel plus the session-scoped dwell/shuffle choices, handles its
// own keys, and renders the cross-fade through a shared FullTexCache. ImageViewer
// composes one of these and drives it while in its Slideshow view mode, so all
// the slideshow concern lives here rather than bloating the viewer.
class SlideshowView {
public:
    // Begin a slideshow of `count` images, first showing `start_index` (running).
    void start(int count, int start_index);
    void stop() { show_.reset(); }

    [[nodiscard]] bool active() const noexcept { return show_.has_value(); }
    [[nodiscard]] int  index() const noexcept { return show_ ? show_->index() : 0; }

    // True while the slideshow needs continuous redraws (auto-advance or an
    // in-flight cross-fade); lets the app loop idle when the slideshow is paused.
    [[nodiscard]] bool animating() const noexcept { return show_ && show_->animating(); }

    void toggle_play() { if (show_) show_->toggle(); }
    void update(double dt) { if (show_) show_->tick(dt); }

    // Handle one key. Returns false when the user asked to leave the slideshow
    // (Esc/Up); the caller then exits to the still viewer at index().
    bool handle_key(SDL_Keycode key);

    // Draw the current frame (cross-faded with the outgoing one) full-screen,
    // plus the status/controls HUD. `images` is the leaf gallery's image list.
    void render(gfx::Renderer& r, gfx::FontAtlas& font, FullTexCache& cache,
                std::span<const vault::IndexNode* const> images,
                float win_w, float win_h);

private:
    void reseed(bool keep_running);   // rebuild the model (e.g. shuffle toggle)

    std::optional<SlideshowModel> show_;
    double dwell_   = SLIDESHOW_DWELL_DEFAULT;
    bool   shuffle_ = false;
    std::vector<uint64_t> keep_scratch_;   // reused per-frame evict keep-list
};

} // namespace ui
