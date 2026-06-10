#pragma once

#include <SDL3/SDL.h>

#include <span>
#include <string_view>

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

/// Higher-level draw operations over an SDL_Renderer. Does not own the renderer.
class Renderer {
public:
    explicit Renderer(SDL_Renderer* r) : r_(r) {}

    [[nodiscard]] SDL_Renderer* sdl() const noexcept { return r_; }

    /// Fill (or outline) a rectangle in colour `c`.
    void draw_rect(const SDL_FRect& dst, Color c, bool filled = true);

    /// Draw the whole texture into `dst`, modulated by `tint`.
    void draw_image(SDL_Texture* tex, const SDL_FRect& dst, Color tint = {});

    /// Draw the `src` sub-rectangle of the texture into `dst`, modulated by `tint`.
    void draw_image(SDL_Texture* tex, const SDL_FRect& src, const SDL_FRect& dst,
                    Color tint = {});

    /// Draw `text` with its top-left at (x, y).
    void draw_text(FontAtlas& font, float x, float y, std::string_view text, Color c);

    /// Lay out `thumbs` left-to-right inside `strip`, each fit into a
    /// `thumb_size` square with `gap` between cells, horizontally scrolled by
    /// `scroll_x`. The cell at index `selected` gets a `highlight` border.
    /// Drawing is clipped to `strip`. Returns the content width (for scroll
    /// clamping by the caller).
    float draw_thumbnail_strip(std::span<SDL_Texture* const> thumbs,
                               const SDL_FRect& strip, float thumb_size, float gap,
                               float scroll_x, int selected, Color highlight);

private:
    SDL_Renderer* r_ = nullptr;
};

} // namespace gfx
