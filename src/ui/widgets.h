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

// Rect of cell `index` in a `cols`-wide grid with the given cell/gap/origin.
[[nodiscard]] SDL_FRect grid_cell_rect(int index, int cols, float cell, float gap,
                                       float origin_x, float origin_y) noexcept;

// Index of the cell under (mx,my), or -1. Considers indices [0,count).
[[nodiscard]] int grid_hit(float mx, float my, int count, int cols, float cell,
                           float gap, float origin_x, float origin_y) noexcept;

// Aspect-fit a `w`x`h` source centred inside `box`.
[[nodiscard]] SDL_FRect fit_rect(float w, float h, const SDL_FRect& box) noexcept;

// --- Thin draw helpers (not unit-tested) ---------------------------------
struct Button { SDL_FRect rect; std::string label; };
void draw_button(gfx::Renderer& r, gfx::FontAtlas& font, const Button& b,
                 bool hover, bool active);
void draw_text_field(gfx::Renderer& r, gfx::FontAtlas& font, const SDL_FRect& box,
                     std::string_view shown, bool focused);

} // namespace ui
