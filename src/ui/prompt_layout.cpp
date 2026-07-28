#include "ui/prompt_layout.h"

#include <algorithm>

#include "ui/text_metrics.h"

namespace ui {
namespace {
constexpr float WIDTH_RATIO = 0.75f;
constexpr float MAX_W       = 900.0f;
constexpr float MIN_W       = 500.0f;
constexpr float PAD         = 16.0f;
constexpr float TITLE_PAD   = 12.0f;
constexpr float INPUT_PAD   = 12.0f;
constexpr float HINT_PAD    = 8.0f;
} // namespace

PromptBoxLayout prompt_box_layout(const PromptBoxSpec& spec)
{
    const float line_h = line_pitch(spec.font_px);
    const float body_h = static_cast<float>(std::max(0, spec.body_lines)) *
                         std::max(0.0f, spec.body_line_h);
    const float middle_h = std::max(0.0f, spec.input_h) + body_h;

    const float w = std::clamp(spec.window_w * WIDTH_RATIO, MIN_W, MAX_W);
    const float h = TITLE_PAD + line_h + INPUT_PAD + middle_h + HINT_PAD + line_h + PAD;

    PromptBoxLayout l;
    l.box     = {.x = (spec.window_w - w) / 2.0f, .y = (spec.window_h - h) / 2.0f,
                 .w = w, .h = h};
    l.line_h  = line_h;
    l.title_y = l.box.y + TITLE_PAD;
    l.input_y = l.title_y + line_h + INPUT_PAD;
    l.body_y  = l.input_y;                       // the two are mutually exclusive
    l.hint_y  = l.input_y + middle_h + HINT_PAD;
    return l;
}

} // namespace ui
