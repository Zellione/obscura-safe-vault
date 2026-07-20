#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "image/decode_worker.h"
#include "ui/export_ui.h"
#include "ui/full_tex_cache.h"
#include "ui/gallery_session_state.h"
#include "ui/gif_model.h"
#include "ui/gif_playback.h"
#include "ui/quick_switch.h"
#include "ui/screen.h"
#include "ui/scroll_model.h"
#include "ui/search_overlay.h"
#include "ui/slideshow_view.h"
#include "ui/strip_layout.h"
#include "ui/tag_editor.h"
#include "ui/video_playback.h"
#include "ui/viewer_model.h"

namespace gfx { class Window; class FontAtlas; class Renderer; class TextureCache; }
namespace vault { class Vault; struct IndexNode; }
namespace platform { class FolderDialog; class VaultRegistry; }

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
    // The viewer's navigable image set. Two ways to populate it:
    //   * gallery mode — set `gallery_path`; the images are listed on entry and
    //     `back`/`paths` are derived (exit returns to that gallery).
    //   * collection mode — set `from_collection` + an explicit `images`/`paths`
    //     set (e.g. favorites) and a `back` target (exit returns there).
    // Bundled into one struct so the viewer carries a single source field and a
    // single constructor (keeps the class within its field/param/method budgets).
    struct Album {
        std::vector<const vault::IndexNode*> images;
        std::vector<std::string>             paths;         // full slash-path per image
        std::string                          gallery_path;  // leaf gallery (gallery mode)
        Nav                                  back;           // exit target (collection mode)
        bool                                 from_collection = false;

        static Album gallery(std::string path)
        {
            Album a;
            a.gallery_path = std::move(path);
            return a;
        }
    };

    // Host-provided collaborators beyond the core render deps, bundled to keep the
    // constructor within the parameter budget (S107). initial_strip_side seeds the
    // session-scoped thumbnail-strip side (Phase 39 Part 2).
    struct Context {
        platform::FolderDialog&  folder;
        platform::VaultRegistry& registry;
        std::string              active_path;
        StripSide                initial_strip_side = StripSide::Bottom;
    };

    ImageViewer(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                gfx::TextureCache& cache, Context ctx, Album album, int start_index);

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
        if (mode_ == ViewMode::Slideshow && slideshow_.animating()) return true;
        if (video_ && video_->animating()) return true;   // a playing video keeps the loop ticking
        return gif_ && gif_->animating();                 // a playing GIF keeps the loop ticking
    }

    [[nodiscard]] std::vector<ui::HelpGroup> help_groups() const override;

private:
    enum class ViewMode { Fit, FillScroll, Slideshow };

    // `sc` carries the physical scancode alongside the layout-resolved keycode so
    // the video/slideshow paths can bind layout-independent keys (volume/dwell,
    // Phase 25).
    void handle_key(SDL_Keycode key, SDL_Scancode sc);
    void handle_key_fit(SDL_Keycode key);
    void handle_key_scroll(SDL_Keycode key);
    void handle_key_video(SDL_Keycode key, SDL_Scancode sc);  // Space/,/./J/L + F/arrows + [ ] volume
    void handle_key_gif(SDL_Keycode key);                      // Space toggles pause
    void handle_mouse_down(const SDL_MouseButtonEvent& b);
    void handle_wheel(const SDL_MouseWheelEvent& w);
    [[nodiscard]] bool handle_overlay_event(const SDL_Event& e);  // modal overlays; true if consumed

    [[nodiscard]] float     thumb_size() const;
    [[nodiscard]] SDL_FRect viewport_rect() const;
    [[nodiscard]] SDL_FRect strip_rect() const;

    void show_image_at(int idx);                    // absolute, clamped, refit (rebuilds video_)
    void set_index(int delta);                      // wrap, reset view, rebuild
    void go_back();                                 // return to gallery / favorites
    void zoom_by(float factor, float cx, float cy); // centred on (cx, cy)
    void pan_by(float dx, float dy);

    // Fill-scroll helpers. The model is cached and rebuilt only when the
    // viewport size changes (the image list is fixed for the session), since
    // render/scroll query it several times per frame.
    [[nodiscard]] const ScrollModel& build_scroll_model();
    [[nodiscard]] float       scaled_height(const vault::IndexNode& n, float vp_w) const;
    void scroll_to_image(int idx);                  // place image `idx` at the top
    void scroll_by(float dy);

    SDL_Texture* thumb_texture(const vault::IndexNode& node);  // shared cache
    [[nodiscard]] bool pump_thumbs();                          // drain worker results into cache
    [[nodiscard]] int strip_hit(float mx, float my) const;     // thumb under cursor, or -1
    void render_fit(gfx::Renderer& r, const SDL_FRect& vp);
    void render_scroll(gfx::Renderer& r, const SDL_FRect& vp);
    void render_strip(gfx::Renderer& r);
    void render_hud(gfx::Renderer& r, const SDL_FRect& vp);

    // Free friends (Phase 39 Part 2), not members, so App can snapshot/restore
    // session-scoped state without growing this class's method count:
    // current_strip_side reads the strip side for GallerySessionState;
    // capture_video_resume snapshots the outgoing viewer's video-resume bookmark
    // (or clears it when the current item isn't a live video); apply_video_resume
    // seeks a freshly (re)opened matching video to a remembered position, leaving
    // it paused, right after on_enter() has built video_ for the landed item.
    friend StripSide current_strip_side(const ImageViewer& v);
    friend void capture_video_resume(const ImageViewer& v, GallerySessionState& session);
    friend void apply_video_resume(ImageViewer& v, const GallerySessionState& session);

    gfx::Window&            win_;
    gfx::FontAtlas&         font_;
    vault::Vault&           vault_;
    gfx::TextureCache&      cache_;
    ExportUi                export_;
    TagEditor               tag_editor_;
    SearchOverlay           search_;
    QuickSwitch             quick_switch_;   // ` overlay: jump to another vault
    Album album_;                            // the navigable image set (see Album)
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

    // Off-thread decoders, scoped to this viewer so their pending decodes are
    // dropped when the viewer closes. Each screen owns separate workers for
    // thumbnail and full-image decodes: they key on the same data_offset, so a
    // shared worker would risk cross-contaminating the two caches.
    struct ThumbDecode {
        image::DecodeWorker          worker{image::decode_wake_event()};
        std::unordered_set<uint64_t> failed;   // thumbs that gave up decoding
    };
    ThumbDecode thumbs_;

    image::DecodeWorker decode_worker_{image::decode_wake_event()};

    // Bounded set of decoded full-res textures (shared with the slideshow).
    FullTexCache full_cache_;

    // Live playback for the current item when it is a video AND we are in Fit mode
    // (null otherwise). pImpl, so this compiles without vendored FFmpeg (valid() is
    // then false and the host shows the poster). Borrows the unlocked vault;
    // destroyed on exit / item change before any lock (invariant: no UAF).
    std::unique_ptr<VideoPlayback> video_;

    // Animated-GIF playback for the current item (Phase 47). Null unless the
    // current image is a GIF with meta.animated set. Same lifetime rules as
    // video_: destroyed before any vault lock / idle / switch.
    std::unique_ptr<GifPlayback> gif_;

    // Tracks which album index the current gif_ was constructed for, so that
    // on scroll the GIF can be torn down and rebuilt before rendering. -1 when
    // gif_ is null. Used to detect and reconcile index changes (Phase 47 fix).
    int gif_index_ = -1;

    // Rebuild gif_ for the current index if it has moved. Called from both
    // show_image_at() and update() to keep gif_ in sync regardless of how
    // index_ changed.
    void sync_gif_for_current_index();

    // Start or stop strip hover animation on the given thumbnail index.
    // Checks both the animated badge and the dimension budget before constructing.
    void start_strip_hover_animation(int strip_thumb);

    // Strip hover animation (Phase 47 Task 10). Independent of the main gif_
    // playback (both may run at once). At most one strip hover animation at a time.
    GifHoverGate                 strip_hover_gate_;
    std::unique_ptr<GifPlayback> strip_hover_gif_;
    int                          strip_hover_gif_index_ = -1;
};

// Free friends of ImageViewer (see the in-class declarations): current_strip_side /
// capture_video_resume / apply_video_resume let App snapshot and restore
// GallerySessionState across a viewer round trip (Phase 39 Part 2).
[[nodiscard]] StripSide current_strip_side(const ImageViewer& v);
void capture_video_resume(const ImageViewer& v, GallerySessionState& session);
void apply_video_resume(ImageViewer& v, const GallerySessionState& session);

} // namespace ui
