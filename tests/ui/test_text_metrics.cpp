// Phase 56: one font-derived text pitch. A hardcoded pitch smaller than the
// baked glyph height is what let popups cut their bottom line — the invariant
// that forbids it is `pitch > font_px`, asserted here across a size sweep
// rather than spot-checked at the one font size the app happens to use today.

#include "test_framework.h"

#include "ui/text_metrics.h"

TEST(line_pitch_exceeds_the_glyph_height_at_every_font_size)
{
    for (float px = 8.0f; px <= 64.0f; px += 1.0f) {
        CHECK(ui::line_pitch(px) > px);
    }
}

TEST(line_pitch_uses_a_125_percent_leading_rounded_up)
{
    CHECK_EQ(ui::line_pitch(28.0f), 35.0f);   // the app's font
    CHECK_EQ(ui::line_pitch(24.0f), 30.0f);
    CHECK_EQ(ui::line_pitch(16.0f), 20.0f);
    CHECK_EQ(ui::line_pitch(10.0f), 13.0f);   // 12.5 rounded up, not down
}

TEST(line_pitch_is_whole_pixels)
{
    for (float px = 8.0f; px <= 64.0f; px += 0.5f) {
        const float p = ui::line_pitch(px);
        CHECK_EQ(p, static_cast<float>(static_cast<int>(p)));
    }
}

TEST(line_pitch_is_zero_for_a_degenerate_font)
{
    CHECK_EQ(ui::line_pitch(0.0f), 0.0f);
    CHECK_EQ(ui::line_pitch(-12.0f), 0.0f);
}

TEST(row_height_is_a_pitch_plus_padding_on_both_sides)
{
    CHECK_EQ(ui::row_height(28.0f, 9.0f), 53.0f);    // 35 + 2*9
    CHECK_EQ(ui::row_height(28.0f, 0.0f), 35.0f);
}

TEST(row_height_ignores_negative_padding)
{
    CHECK_EQ(ui::row_height(28.0f, -5.0f), 35.0f);
}
