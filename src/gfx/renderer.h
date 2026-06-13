#pragma once

#include <SDL3/SDL.h>

#include <span>
#include <string_view>
#include <vector>

#include "gfx/color.h"
#include "gfx/text.h"

namespace gfx {

/// Pure layout: total pixel width of `count` thumbnails of side `thumb_size`
/// separated by `gap` (no trailing gap). Headless / unit-testable.
[[nodiscard]] constexpr float
thumbnail_strip_content_width(int count, float thumb_size, float gap) noexcept
{
    if (count <= 0) return 0.0f;
    return static_cast<float>(count) * thumb_size +
           static_cast<float>(count - 1) * gap;
}

/// Outline polygon (closed loop) of a rounded rectangle: the 4 corners replaced
/// by `segments`-step arcs. The loop touches all four rectangle edges, so its
/// bounding box equals `dst`. `radius` is clamped to min(w,h)/2; radius <= 0
/// yields the 4 plain corners. Pure / unit-testable. The renderer fans this into
/// triangles (filled) or strokes it (outline).
[[nodiscard]] std::vector<SDL_FPoint>
round_rect_outline(const SDL_FRect& dst, float radius, int segments = 6);

/// Parameters for draw_thumbnail_strip (grouped to keep the call signature small).
struct ThumbnailStrip {
    float size      = 0.0f;    // thumbnail side length (square cell)
    float gap       = 0.0f;    // gap between cells
    float scroll    = 0.0f;    // scroll offset along the long axis
    int   selected  = -1;      // index of the highlighted cell
    Color highlight {};        // selection-glow colour
    bool  vertical  = false;   // lay out top-to-bottom instead of left-to-right
};

/// Higher-level draw operations over an SDL_Renderer. Does not own the renderer.
class Renderer {
public:
    explicit Renderer(SDL_Renderer* r) : r_(r) {}

    [[nodiscard]] SDL_Renderer* sdl() const noexcept { return r_; }

    /// Fill (or outline) a rectangle in colour `c`.
    void draw_rect(const SDL_FRect& dst, Color c, bool filled = true);

    /// Fill (or outline) a rounded rectangle of corner `radius` in colour `c`.
    /// radius <= 0 falls through to a square draw_rect.
    void draw_round_rect(const SDL_FRect& dst, float radius, Color c,
                         bool filled = true);

    /// Soft selection halo: a few progressively fainter rounded-rect outlines
    /// just outside `dst`. No shaders / render targets.
    void draw_selection_glow(const SDL_FRect& dst, float radius, Color accent);

    /// Draw the whole texture into `dst`, modulated by `tint`.
    void draw_image(SDL_Texture* tex, const SDL_FRect& dst, Color tint = {});

    /// Draw the `src` sub-rectangle of the texture into `dst`, modulated by `tint`.
    void draw_image(SDL_Texture* tex, const SDL_FRect& src, const SDL_FRect& dst,
                    Color tint = {});

    /// Draw `text` with its top-left at (x, y).
    void draw_text(FontAtlas& font, float x, float y, std::string_view text, Color c);

    /// Lay out `thumbs` inside `strip`, each fit into a `ThumbnailStrip::size`
    /// square with `gap` between cells, scrolled by `scroll` along the strip's
    /// long axis (horizontal, or vertical when `vertical` is true). The cell at
    /// index `selected` gets a `highlight` selection glow. Drawing is clipped to
    /// `strip`. Returns the content length (for scroll clamping by the caller).
    float draw_thumbnail_strip(std::span<SDL_Texture* const> thumbs,
                               const SDL_FRect& strip, const ThumbnailStrip& opts);

private:
    SDL_Renderer* r_ = nullptr;
};

} // namespace gfx
