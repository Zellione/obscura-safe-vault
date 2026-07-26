#include "ui/text_field_view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#include "ui/text_input_model.h"

namespace ui {

namespace {

std::span<const uint8_t> as_bytes(std::string_view s) noexcept
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

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

PrefixWidths build_prefix_widths(std::string_view text, const TextMeasureFn& measure)
{
    PrefixWidths pw;
    const auto bytes = as_bytes(text);
    pw.offset.push_back(0);
    pw.width.push_back(0.0f);

    size_t i = 0;
    float  w = 0.0f;
    while (i < text.size()) {
        const size_t next = utf8_next_boundary(bytes, i);
        w += static_cast<float>(measure(text.substr(i, next - i)));
        pw.offset.push_back(next);
        pw.width.push_back(w);
        i = next;
    }
    return pw;
}

} // namespace

TextFieldLayout layout_text_field(std::string_view text, size_t caret,
                                  size_t sel_begin, size_t sel_end,
                                  float field_w, float prev_scroll,
                                  const TextMeasureFn& measure)
{
    TextFieldLayout out;
    if (field_w <= 0.0f) return out;

    const PrefixWidths pw = build_prefix_widths(text, measure);
    const float total      = pw.width.back();
    const float max_scroll = std::max(0.0f, total - field_w);

    // Follow the caret at both edges, then clamp so the field never scrolls past
    // the end of the text (or before its start).
    const float caret_abs = pw.at(std::min(caret, text.size()));
    float scroll = std::clamp(prev_scroll, 0.0f, max_scroll);
    if (caret_abs < scroll)                scroll = caret_abs;
    else if (caret_abs > scroll + field_w) scroll = caret_abs - field_w;
    scroll = std::clamp(scroll, 0.0f, max_scroll);

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
    out.caret_visible = out.caret_x >= 0.0f && out.caret_x <= field_w;

    if (sel_begin < sel_end) {
        const float x0 = std::clamp(pw.at(sel_begin) - scroll, 0.0f, field_w);
        const float x1 = std::clamp(pw.at(sel_end) - scroll, 0.0f, field_w);
        out.sel_x = x0;
        out.sel_w = std::max(0.0f, x1 - x0);
    }
    return out;
}

bool caret_is_on(uint64_t now_ms, uint64_t last_edit_ms) noexcept
{
    // Clock skew (or a chrome struct that has never seen an edit) must not make
    // the caret vanish; treat "before the last edit" as "just edited".
    const uint64_t since = now_ms >= last_edit_ms ? now_ms - last_edit_ms : 0;
    return (since / CARET_BLINK_MS) % 2 == 0;
}

} // namespace ui
