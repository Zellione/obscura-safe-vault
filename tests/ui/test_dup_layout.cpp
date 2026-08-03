#include "test_framework.h"

#include "ui/dup_layout.h"
#include "ui/text_metrics.h"

// Two members split the full content width between them.
TEST(dup_layout_two_members_fill_width)
{
    const auto l = ui::dup_row_layout(40.0f, 1320.0f, 345.0f, 28.0f, 2);
    CHECK_EQ(l.tile_h, 345.0f);
    CHECK(l.tile_w > 600.0f);
    // Block spans exactly the content band and starts at its left edge.
    const float block = 2 * l.tile_w + ui::DUP_TILE_GAP;
    CHECK(block <= 1320.0f + 0.5f);
    CHECK(block >= 1320.0f - 1.0f);
    CHECK_EQ(l.first_x, 40.0f);
}

// More members scale the tile width down, never past the readable floor.
TEST(dup_layout_more_members_scale_down)
{
    const auto two  = ui::dup_row_layout(40.0f, 1320.0f, 345.0f, 28.0f, 2);
    const auto four = ui::dup_row_layout(40.0f, 1320.0f, 345.0f, 28.0f, 4);
    CHECK(four.tile_w < two.tile_w);

    const auto many = ui::dup_row_layout(40.0f, 1320.0f, 345.0f, 28.0f, 40);
    CHECK(many.tile_w >= ui::DUP_MIN_TILE_W);
    // Floor makes the block overflow; the row must anchor at the band's left
    // edge, never at a negative x.
    CHECK_EQ(many.first_x, 40.0f);
}

// The row advance reserves every drawn pixel: header line, tile, three
// font-derived text lines, and the inter-group gap. This is the invariant the
// hardcoded TEXT_LINES_H=50 broke (three 28 px lines need ~105 px).
TEST(dup_layout_row_height_holds_all_content)
{
    const float px = 28.0f;
    const auto l = ui::dup_row_layout(40.0f, 1320.0f, 345.0f, px, 3);
    CHECK(l.header_h >= ui::line_pitch(px));
    CHECK(l.text_h >= 3.0f * ui::line_pitch(px));
    CHECK(l.row_h >= l.header_h + l.tile_h + l.text_h + ui::DUP_GROUP_GAP);
}

// Tile height follows the window but stays inside sane bounds.
TEST(dup_layout_tile_height_from_window)
{
    CHECK_EQ(ui::dup_tile_height(0.0f), 180.0f);      // tiny window clamps low
    CHECK_EQ(ui::dup_tile_height(3000.0f), 440.0f);   // huge window clamps high
    const float mid = ui::dup_tile_height(900.0f);
    CHECK(mid > 180.0f);
    CHECK(mid < 440.0f);
    CHECK(ui::dup_tile_height(1100.0f) >= mid);       // monotonic
}

// A degenerate zero-member row still yields a positive, finite layout.
TEST(dup_layout_zero_members_safe)
{
    const auto l = ui::dup_row_layout(40.0f, 1320.0f, 345.0f, 28.0f, 0);
    CHECK(l.tile_w > 0.0f);
    CHECK(l.row_h > 0.0f);
}

// Footer band reserves three font-derived lines (status, marked totals, key
// hints) — the 20 px hardcoded spacing overlapped 28 px glyphs.
TEST(dup_layout_footer_height_from_font)
{
    const float h = ui::dup_footer_height(28.0f);
    CHECK(h >= 3.0f * ui::line_pitch(28.0f));
    CHECK(ui::dup_footer_height(40.0f) > h);
}
