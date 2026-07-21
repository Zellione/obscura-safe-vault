#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <string_view>

#include "gfx/color.h"

namespace gfx { class Renderer; class FontAtlas; }

namespace ui {

// --- Pure layout / hit-testing -------------------------------------------
[[nodiscard]] bool point_in_rect(float x, float y, const SDL_FRect& r) noexcept;

// How many `cell`-wide columns (separated by `gap`) fit in `avail_w` (min 1).
[[nodiscard]] int grid_columns(float avail_w, float cell, float gap) noexcept;

// Geometry of a uniform tile grid: `cols` columns of `cell`-wide square cells
// separated by `gap`, laid out from the top-left `origin_x`/`origin_y`.
struct GridSpec {
    int   cols;
    float cell;
    float gap;
    float origin_x;
    float origin_y;
};

// Rect of cell `index` in the given grid.
[[nodiscard]] SDL_FRect grid_cell_rect(int index, const GridSpec& g) noexcept;

// Index of the cell under (mx,my), or -1. Considers indices [0,count).
[[nodiscard]] int grid_hit(float mx, float my, int count, const GridSpec& g) noexcept;

// Aspect-fit a `w`x`h` source centred inside `box`.
[[nodiscard]] SDL_FRect fit_rect(float w, float h, const SDL_FRect& box) noexcept;

// --- Thin draw helpers (not unit-tested) ---------------------------------
struct Button { SDL_FRect rect; std::string label; };
void draw_button(gfx::Renderer& r, gfx::FontAtlas& font, const Button& b,
                 bool hover, bool active);
void draw_text_field(gfx::Renderer& r, gfx::FontAtlas& font, const SDL_FRect& box,
                     std::string_view shown, bool focused);

// Fill one of the reserved chrome bands from ui/chrome_layout.h with an OPAQUE
// `fill`, plus a hairline rule along the edge facing the content area
// (`rule_at_bottom` for a header band, false for a footer band). Opaque is the
// point: a translucent band lets the image or tiles behind it wash the band's
// text out. No-op for an empty band, so a caller can pass a collapsed rect
// (e.g. the viewer's chrome-free fullscreen bands) unconditionally.
void draw_chrome_band(gfx::Renderer& r, const SDL_FRect& band, gfx::Color fill,
                      bool rule_at_bottom);

// Hover/active state for a button under the mouse. `active` (pressed) requires
// the cursor to be over the button while the mouse button is held down.
struct ButtonState { bool hover = false; bool active = false; };
[[nodiscard]] ButtonState button_state(const SDL_FRect& rect, float mx, float my,
                                        bool mouse_down) noexcept;

// Truncate `name` in the middle with "..." so its measured width fits within
// `max_w` px. `measure(string_view) -> int` returns the pixel width of a string
// (e.g. FontAtlas::measure). Returns the name unchanged if it already fits, or
// "" if even the ellipsis alone won't fit. Pure / unit-testable via a fake
// measure (templated on the callable to avoid a std::function allocation).
template <class Measure>
[[nodiscard]] std::string elide_middle(std::string_view name, int max_w, Measure&& measure)
{
    if (measure(name) <= max_w) return std::string(name);

    constexpr std::string_view ell = "...";
    if (measure(ell) > max_w) return std::string();

    // Shrink the kept character count, splitting evenly between head and tail
    // (head gets the odd one), until the "head…tail" form fits.
    const auto n = static_cast<int>(name.size());
    for (int keep = n - 1; keep >= 0; --keep) {
        const int head = (keep + 1) / 2;
        const int tail = keep / 2;
        std::string cand;
        cand.append(name.substr(0, static_cast<size_t>(head)));
        cand.append(ell);
        cand.append(name.substr(static_cast<size_t>(n - tail)));
        if (measure(cand) <= max_w) return cand;
    }
    return std::string(ell);
}

// elide_middle bound to a real font: fit `s` into `max_w` px measured by `font`.
[[nodiscard]] std::string fit_text(const gfx::FontAtlas& font, std::string_view s,
                                   float max_w);

} // namespace ui
