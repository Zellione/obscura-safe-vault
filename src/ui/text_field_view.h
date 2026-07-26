#pragma once

// Pure layout for an editable single-line text field: where the visible run of
// text starts and ends, where the caret sits, and where the selection highlight
// goes. SDL-free and gfx-free — it takes a width-measuring callable (in practice
// gfx::FontAtlas::measure) so it is unit-testable with a stub measurer.
//
// The caret maths use the SAME measure() the renderer draws with, so the caret
// stays consistent with what is actually on screen. That matters here: the baked
// atlas covers printable ASCII only (gfx::FIRST_GLYPH..126), so a non-ASCII
// character measures 0 and draws nothing. It is stored correctly and round-trips
// through the vault; it simply has no glyph. Extending the atlas is deliberately
// out of scope for Phase 54.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace ui {

// Pixel width of a run of text, as the renderer would draw it.
using TextMeasureFn = std::function<int(std::string_view)>;

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

// Lay `text` out in a field `field_w` pixels wide. `prev_scroll` is the scroll
// this field carried last frame; the result's `scroll` is what to carry into the
// next one, so the view follows the caret at both edges instead of jumping back
// to the start whenever the caret moves.
//
// Partially visible characters are excluded from [vis_begin, vis_end) so the run
// can be drawn without a clip rect — the text simply ends at the field edge.
[[nodiscard]] TextFieldLayout layout_text_field(std::string_view text, size_t caret,
                                                size_t sel_begin, size_t sel_end,
                                                float field_w, float prev_scroll,
                                                const TextMeasureFn& measure);

// Caret blink, expressed against a monotonic millisecond clock rather than a
// per-frame dt: the caret is solid for the first half-period after the last
// edit, so it never blinks out just as the user types, and no host has to thread
// a dt into its dialog to get that.
inline constexpr uint64_t CARET_BLINK_MS = 500;

[[nodiscard]] bool caret_is_on(uint64_t now_ms, uint64_t last_edit_ms) noexcept;

} // namespace ui
