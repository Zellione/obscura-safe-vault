#pragma once

// Phase 56: geometry for a centred prompt/summary box — the shape tag_overview
// uses for its description-edit prompt and its import-summary modal. Both boxes
// derived their own copy of this arithmetic, and both laid title and hint lines
// out on a 20 px pitch under a 28 px font, so the text ran into its neighbours.
// Pure, so the stacking and containment rules are unit tests.

#include <SDL3/SDL.h>

namespace ui {

// A box is: title, then EITHER an input field (input_h > 0) OR `body_lines`
// lines of `body_line_h`, then a hint line. Both may be absent.
struct PromptBoxSpec {
    float font_px     = 0.0f;
    float window_w    = 0.0f;
    float window_h    = 0.0f;
    float input_h     = 0.0f;   // 0 = no input field
    int   body_lines  = 0;      // 0 = no body block
    float body_line_h = 0.0f;
};

struct PromptBoxLayout {
    SDL_FRect box{};
    float     title_y = 0.0f;
    float     input_y = 0.0f;   // meaningful only when spec.input_h > 0
    float     body_y  = 0.0f;   // meaningful only when spec.body_lines > 0
    float     hint_y  = 0.0f;
    float     line_h  = 0.0f;   // pitch used for the title and hint lines
};

[[nodiscard]] PromptBoxLayout prompt_box_layout(const PromptBoxSpec& spec);

} // namespace ui
