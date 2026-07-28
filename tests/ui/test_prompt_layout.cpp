// Phase 56: the centred prompt/summary box geometry, extracted from the two
// places in tag_overview.cpp that duplicated it. The title and hint lines were
// laid out on a 20 px pitch under a 28 px font, so their ink ran into the
// neighbouring element and out of the box. Non-overlap and containment are
// asserted here.

#include "test_framework.h"

#include "ui/prompt_layout.h"

namespace {
constexpr float FONT_PX = 28.0f;

ui::PromptBoxSpec edit_prompt()
{
    return {.font_px = FONT_PX, .window_w = 1280.0f, .window_h = 800.0f,
            .input_h = 32.0f, .body_lines = 0, .body_line_h = 0.0f};
}

ui::PromptBoxSpec summary_box(int lines)
{
    return {.font_px = FONT_PX, .window_w = 1280.0f, .window_h = 800.0f,
            .input_h = 0.0f, .body_lines = lines, .body_line_h = FONT_PX + 8.0f};
}
} // namespace

TEST(prompt_box_stacks_title_input_and_hint_without_overlap)
{
    const ui::PromptBoxLayout l = ui::prompt_box_layout(edit_prompt());
    CHECK(l.title_y + FONT_PX <= l.input_y);
    CHECK(l.input_y + 32.0f <= l.hint_y);
}

TEST(prompt_box_contains_every_line_it_lays_out)
{
    const ui::PromptBoxLayout l = ui::prompt_box_layout(edit_prompt());
    CHECK(l.title_y >= l.box.y);
    CHECK(l.hint_y + FONT_PX <= l.box.y + l.box.h);
}

TEST(prompt_box_is_centred_in_the_window)
{
    const ui::PromptBoxLayout l = ui::prompt_box_layout(edit_prompt());
    CHECK_EQ(l.box.x + l.box.w / 2.0f, 640.0f);
    CHECK_EQ(l.box.y + l.box.h / 2.0f, 400.0f);
}

TEST(prompt_box_width_honours_the_ratio_and_its_bounds)
{
    ui::PromptBoxSpec narrow = edit_prompt();
    narrow.window_w = 400.0f;                 // 75% would be 300, below the 500 floor
    CHECK_EQ(ui::prompt_box_layout(narrow).box.w, 500.0f);

    ui::PromptBoxSpec wide = edit_prompt();
    wide.window_w = 4000.0f;                  // 75% would be 3000, above the 900 ceiling
    CHECK_EQ(ui::prompt_box_layout(wide).box.w, 900.0f);
}

TEST(summary_box_grows_with_its_body_lines)
{
    const float h1 = ui::prompt_box_layout(summary_box(1)).box.h;
    const float h5 = ui::prompt_box_layout(summary_box(5)).box.h;
    CHECK_EQ(h5 - h1, 4.0f * (FONT_PX + 8.0f));
}

TEST(summary_box_puts_its_body_between_title_and_hint)
{
    const ui::PromptBoxLayout l = ui::prompt_box_layout(summary_box(3));
    CHECK(l.title_y + FONT_PX <= l.body_y);
    CHECK(l.body_y + 3.0f * (FONT_PX + 8.0f) <= l.hint_y);
    CHECK(l.hint_y + FONT_PX <= l.box.y + l.box.h);
}

TEST(prompt_box_line_pitch_clears_the_glyph_box)
{
    const ui::PromptBoxLayout l = ui::prompt_box_layout(edit_prompt());
    CHECK(l.line_h > FONT_PX);
}
