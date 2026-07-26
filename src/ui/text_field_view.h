#pragma once

// Pure layout for an editable single-line text field: where the visible run of
// text starts and ends, where the caret sits, and where the selection highlight
// goes. SDL-free and gfx-free — it takes a width-measuring callable (in practice
// gfx::FontAtlas::measure) so it is unit-testable with a stub measurer.
//
// `layout_text_field` is templated on the callable rather than taking a
// std::function, matching ui::elide_middle in widgets.h and for the same reason:
// it runs per field per frame, and a std::function would allocate for nothing.
//
// The caret maths use the SAME measure() the renderer draws with, so the caret
// stays consistent with what is actually on screen. That matters here: the baked
// atlas covers printable ASCII only (gfx::FIRST_GLYPH..126), so a non-ASCII
// character measures 0 and draws nothing. It is stored correctly and round-trips
// through the vault; it simply has no glyph. Extending the atlas is deliberately
// out of scope for Phase 54.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "ui/text_input_model.h"

namespace ui {

struct TextFieldLayout {
    size_t vis_begin = 0;    // byte offset of the first fully visible character
    size_t vis_end   = 0;    // one past the last fully visible character
    float  scroll    = 0;    // pixels of text scrolled off to the left
    float  text_x    = 0;    // x of vis_begin, relative to the content origin (>= 0)
    float  caret_x   = 0;    // caret x, relative to the content origin
    bool   caret_visible = false;  // false when the caret scrolled out of the field
    float  sel_x     = 0;    // selection highlight x, clipped into [0, field_w]
    float  sel_w     = 0;    // 0 when nothing selected, or the selection is off-screen
};

namespace detail {

// Cumulative pixel width at every character boundary of `text`, built in a
// single pass. gfx::FontAtlas::measure is a plain sum of advances with no
// kerning, so per-character widths add up exactly to the width of any prefix —
// which turns the otherwise quadratic "measure every prefix" layout into O(n).
struct PrefixWidths {
    std::vector<size_t> offset;   // byte offset of each boundary, 0 .. text.size()
    std::vector<float>  width;    // width of text[0, offset[i])

    [[nodiscard]] float at(size_t byte_offset) const noexcept
    {
        const auto it = std::ranges::lower_bound(offset, byte_offset);
        if (it == offset.end()) return width.back();
        return width[static_cast<size_t>(it - offset.begin())];
    }
};

template <class Measure>
[[nodiscard]] PrefixWidths build_prefix_widths(std::string_view text, const Measure& measure)
{
    PrefixWidths pw;
    const std::span<const uint8_t> bytes{reinterpret_cast<const uint8_t*>(text.data()), text.size()};
    pw.offset.push_back(0);
    pw.width.push_back(0.0F);

    size_t i = 0;
    float  w = 0.0F;
    while (i < text.size()) {
        const size_t next = utf8_next_boundary(bytes, i);
        w += static_cast<float>(measure(text.substr(i, next - i)));
        pw.offset.push_back(next);
        pw.width.push_back(w);
        i = next;
    }
    return pw;
}

} // namespace detail

// Lay `text` out in a field `field_w` pixels wide. `prev_scroll` is the scroll
// this field carried last frame; the result's `scroll` is what to carry into the
// next one, so the view follows the caret at both edges instead of jumping back
// to the start whenever the caret moves.
//
// Partially visible characters are excluded from [vis_begin, vis_end) so the run
// can be drawn without a clip rect — the text simply ends at the field edge.
template <class Measure>
[[nodiscard]] TextFieldLayout layout_text_field(std::string_view text, size_t caret,
                                                size_t sel_begin, size_t sel_end,
                                                float field_w, float prev_scroll,
                                                const Measure& measure)
{
    TextFieldLayout out;
    if (field_w <= 0.0F) return out;

    const detail::PrefixWidths pw = detail::build_prefix_widths(text, measure);
    const float total      = pw.width.back();
    const float max_scroll = std::max(0.0F, total - field_w);

    // Follow the caret at both edges, then clamp so the field never scrolls past
    // the end of the text (or before its start).
    const float caret_abs = pw.at(std::min(caret, text.size()));
    float scroll = std::clamp(prev_scroll, 0.0F, max_scroll);
    if (caret_abs < scroll)                scroll = caret_abs;
    else if (caret_abs > scroll + field_w) scroll = caret_abs - field_w;
    scroll = std::clamp(scroll, 0.0F, max_scroll);

    // First character whose left edge is at or past the scroll position, and the
    // last one that fits entirely: partial glyphs are excluded at both ends, so
    // the run can be drawn without a clip rect.
    size_t bi = 0;
    while (bi + 1 < pw.offset.size() && pw.width[bi] < scroll) ++bi;
    size_t ei = bi;
    while (ei + 1 < pw.offset.size() && pw.width[ei + 1] - scroll <= field_w) ++ei;

    out.vis_begin = pw.offset[bi];
    out.vis_end   = pw.offset[ei];
    out.scroll    = scroll;
    out.text_x    = pw.width[bi] - scroll;
    out.caret_x   = caret_abs - scroll;
    out.caret_visible = out.caret_x >= 0.0F && out.caret_x <= field_w;

    if (sel_begin < sel_end) {
        const float x0 = std::clamp(pw.at(sel_begin) - scroll, 0.0F, field_w);
        const float x1 = std::clamp(pw.at(sel_end) - scroll, 0.0F, field_w);
        out.sel_x = x0;
        out.sel_w = std::max(0.0F, x1 - x0);
    }
    return out;
}

// Caret blink, expressed against a monotonic millisecond clock rather than a
// per-frame dt: the caret is solid for the first half-period after the last
// edit, so it never blinks out just as the user types, and no host has to thread
// a dt into its dialog to get that.
inline constexpr uint64_t CARET_BLINK_MS = 500;

[[nodiscard]] bool caret_is_on(uint64_t now_ms, uint64_t last_edit_ms) noexcept;

} // namespace ui
