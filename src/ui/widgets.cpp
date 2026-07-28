#include "ui/widgets.h"

#include <algorithm>
#include <cmath>

#include <string>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "ui/text_field_view.h"
#include "ui/text_input_model.h"

namespace ui {

namespace {

// Inset of the text from the field box's left edge; also the caret's home when
// the field is empty.
constexpr float FIELD_PAD = 12.0f;
constexpr float CARET_W   = 2.0f;

} // namespace

bool point_in_rect(float x, float y, const SDL_FRect& r) noexcept
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

int grid_columns(float avail_w, float cell, float gap) noexcept
{
    if (cell <= 0.0f) return 1;
    const auto cols = static_cast<int>(std::floor((avail_w + gap) / (cell + gap)));
    return cols < 1 ? 1 : cols;
}

SDL_FRect grid_cell_rect(int index, const GridSpec& g) noexcept
{
    const int cols = g.cols < 1 ? 1 : g.cols;
    const int row = index / cols;
    const int col = index % cols;
    return SDL_FRect{g.origin_x + static_cast<float>(col) * (g.cell + g.gap),
                     g.origin_y + static_cast<float>(row) * (g.cell + g.gap),
                     g.cell, g.cell};
}

int grid_hit(float mx, float my, int count, const GridSpec& g) noexcept
{
    for (int i = 0; i < count; ++i)
        if (point_in_rect(mx, my, grid_cell_rect(i, g)))
            return i;
    return -1;
}

SDL_FRect fit_rect(float w, float h, const SDL_FRect& box) noexcept
{
    if (w <= 0.0f || h <= 0.0f) return box;
    const float scale = std::min(box.w / w, box.h / h);
    const float nw = w * scale;
    const float nh = h * scale;
    return SDL_FRect{box.x + (box.w - nw) * 0.5f, box.y + (box.h - nh) * 0.5f, nw, nh};
}

void draw_button(gfx::Renderer& r, gfx::FontAtlas& font, const Button& b,
                 bool hover, bool active)
{
    using namespace gfx::theme;
    gfx::Color bg = SURFACE;
    if (active)     bg = ACCENT_DIM;
    else if (hover) bg = SURFACE_HI;
    r.draw_round_rect(b.rect, RADIUS_SMALL, bg);
    r.draw_round_rect(b.rect, RADIUS_SMALL, (hover || active) ? ACCENT : BORDER,
                      /*filled*/ false);
    const int tw = font.measure(b.label);
    r.draw_text(font, b.rect.x + (b.rect.w - static_cast<float>(tw)) * 0.5f,
                font.text_top_for_center(b.rect.y + b.rect.h * 0.5f), b.label, TEXT);
}

void draw_text_field(gfx::Renderer& r, gfx::FontAtlas& font, const SDL_FRect& box,
                     std::string_view shown, bool focused)
{
    using namespace gfx::theme;
    r.draw_round_rect(box, RADIUS_SMALL, SURFACE);
    r.draw_round_rect(box, RADIUS_SMALL, focused ? ACCENT : BORDER, /*filled*/ false);
    r.draw_text(font, box.x + 12.0f, font.text_top_for_center(box.y + box.h * 0.5f),
                shown, TEXT);
}

namespace {

// Where a field's content goes: the text's draw origin, the run width, and the
// band the caret/selection highlight fill. Grouped so paint_field_content stays
// within the parameter budget (cpp:S107) and so the two entry points pass the
// geometry as one thing rather than five loose floats.
struct FieldGeometry {
    float x      = 0;   // left edge of the text run
    float text_y = 0;   // draw_text top for the run
    float w      = 0;   // width available to the run
    float band_y = 0;   // top of the caret / selection band
    float band_h = 0;   // its height
};

// Shared body of draw_edit_field / draw_inline_edit_text: the selection band,
// the visible run of text, and the caret, all laid out by layout_text_field so
// the two entry points cannot disagree about where the caret goes.
void paint_field_content(gfx::Renderer& r, gfx::FontAtlas& font, const FieldGeometry& g,
                         const ITextInput& field, TextFieldChrome& chrome,
                         bool focused, bool mask)
{
    using namespace gfx::theme;

    const auto  raw   = field.bytes();
    const auto* chars = reinterpret_cast<const char*>(raw.data());

    // A masked field lays out one '*' per CHARACTER, and its caret/selection
    // offsets are counted in characters too — byte offsets would put the caret
    // in the wrong place the moment a multi-byte character is typed.
    std::string      masked;
    std::string_view shown(chars, raw.size());
    size_t caret = field.caret();
    size_t sel_b = field.sel_begin();
    size_t sel_e = field.sel_end();
    if (mask) {
        masked.assign(utf8_char_count(raw), '*');
        shown = masked;
        caret = utf8_char_count(raw.first(caret));
        sel_b = utf8_char_count(raw.first(sel_b));
        sel_e = utf8_char_count(raw.first(sel_e));
    }

    const TextFieldLayout L = layout_text_field(
        shown, caret, sel_b, sel_e, g.w, chrome.scroll,
        [&font](std::string_view t) { return font.measure(t); });
    chrome.scroll = L.scroll;

    if (L.sel_w > 0.0f) r.draw_rect({g.x + L.sel_x, g.band_y, L.sel_w, g.band_h}, ACCENT_DIM);
    r.draw_text(font, g.x + L.text_x, g.text_y,
                shown.substr(L.vis_begin, L.vis_end - L.vis_begin), TEXT);

    // Solid for CARET_BLINK_MS after anything changes, then blinking. The change
    // is detected by comparing against what the last frame drew, so no host has
    // to report its edits.
    if (chrome.seen_caret != field.caret() || chrome.seen_size != raw.size()) {
        chrome.seen_caret   = field.caret();
        chrome.seen_size    = raw.size();
        chrome.last_edit_ms = SDL_GetTicks();
    }
    if (focused && L.caret_visible && caret_is_on(SDL_GetTicks(), chrome.last_edit_ms))
        r.draw_rect({g.x + L.caret_x, g.band_y, CARET_W, g.band_h}, ACCENT);
}

} // namespace

void draw_edit_field(gfx::Renderer& r, gfx::FontAtlas& font, const SDL_FRect& box,
                     const ITextInput& field, TextFieldChrome& chrome,
                     bool focused, bool mask)
{
    using namespace gfx::theme;
    r.draw_round_rect(box, RADIUS_SMALL, SURFACE);
    r.draw_round_rect(box, RADIUS_SMALL, focused ? ACCENT : BORDER, /*filled*/ false);

    paint_field_content(r, font,
                        {.x      = box.x + FIELD_PAD,
                         .text_y = font.text_top_for_center(box.y + box.h * 0.5f),
                         .w      = box.w - 2 * FIELD_PAD,
                         .band_y = box.y + 4.0f,
                         .band_h = box.h - 8.0f},
                        field, chrome, focused, mask);
}

void draw_inline_edit_text(gfx::Renderer& r, gfx::FontAtlas& font, float x, float y,
                           float max_w, const ITextInput& field, TextFieldChrome& chrome)
{
    // The band brackets the glyph cell the text is drawn into, so the caret and
    // any selection line up with the run rather than floating above it.
    paint_field_content(r, font,
                        {.x = x, .text_y = y, .w = max_w,
                         .band_y = y, .band_h = font.pixel_height()},
                        field, chrome, /*focused*/ true, /*mask*/ false);
}

void draw_chrome_band(gfx::Renderer& r, const SDL_FRect& band, gfx::Color fill,
                      bool rule_at_bottom)
{
    if (band.h <= 0.0f || band.w <= 0.0f) return;   // collapsed band (e.g. fullscreen)

    // `fill` is drawn at full alpha on purpose — see draw_chrome_band's contract.
    r.draw_rect(band, fill);
    const float rule_y = rule_at_bottom ? band.y + band.h - 1.0f : band.y;
    r.draw_rect({band.x, rule_y, band.w, 1.0f}, gfx::theme::BORDER);
}

std::string fit_text(const gfx::FontAtlas& font, std::string_view s, float max_w)
{
    return elide_middle(s, static_cast<int>(max_w),
                        [&font](std::string_view t) { return font.measure(t); });
}

std::string fit_text_tail(const gfx::FontAtlas& font, std::string_view s, float max_w)
{
    return elide_tail(s, static_cast<int>(max_w),
                      [&font](std::string_view t) { return font.measure(t); });
}

ButtonState button_state(const SDL_FRect& rect, float mx, float my,
                         bool mouse_down) noexcept
{
    const bool hover = point_in_rect(mx, my, rect);
    return {hover, hover && mouse_down};
}

} // namespace ui
