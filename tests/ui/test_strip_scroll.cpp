#include "test_framework.h"

#include "ui/strip_scroll.h"

TEST(strip_wheel_scrolls_and_clamps_to_content)
{
    // thumb 30, gap 10 -> pitch 40. 10 items -> content 390 (10*30 + 9*10).
    // extent 100 (window width).
    const int count = 10;
    const float thumb = 30.0f;
    const float gap = 10.0f;
    const float content = ui::strip_content_extent(count, thumb, gap);
    CHECK_EQ(content, 390.0f);
    const float extent = 100.0f;
    const float step = 27.0f;

    ui::StripScrollState st;

    // Initial state: offset 0, not manual.
    CHECK_EQ(st.offset, 0.0f);
    CHECK_EQ(st.manual, false);

    // Wheel down (w.y < 0) scrolls forward (offset increases).
    ui::strip_apply_wheel(st, -1.0f, step, extent, content);
    CHECK_EQ(st.offset, 27.0f);
    CHECK_EQ(st.manual, true);

    // Another wheel down adds more offset.
    ui::strip_apply_wheel(st, -1.0f, step, extent, content);
    CHECK_EQ(st.offset, 54.0f);
    CHECK_EQ(st.manual, true);

    // Wheel up (w.y > 0) scrolls backward (offset decreases).
    ui::strip_apply_wheel(st, 1.0f, step, extent, content);
    CHECK_EQ(st.offset, 27.0f);
    CHECK_EQ(st.manual, true);

    // Clamp at max: max offset = max(0, 390 - 100) = 290.
    ui::strip_apply_wheel(st, -10.0f, step, extent, content);
    CHECK_EQ(st.offset, 290.0f);
    CHECK_EQ(st.manual, true);

    // Clamp at min: use enough wheel to reach 0.
    ui::strip_apply_wheel(st, 15.0f, step, extent, content);
    CHECK_EQ(st.offset, 0.0f);
    CHECK_EQ(st.manual, true);
}

TEST(strip_wheel_does_nothing_when_content_fits)
{
    // thumb 30, gap 10, count 3 -> content 90. extent 200 (window larger).
    const int count = 3;
    const float thumb = 30.0f;
    const float gap = 10.0f;
    const float content = ui::strip_content_extent(count, thumb, gap);
    const float extent = 200.0f;

    ui::StripScrollState st;

    // Content fits entirely; wheel does nothing and manual stays false.
    ui::strip_apply_wheel(st, -1.0f, 27.0f, extent, content);
    CHECK_EQ(st.offset, 0.0f);
    CHECK_EQ(st.manual, false);
}

TEST(strip_follow_index_reengages_auto_centering)
{
    ui::StripScrollState st;
    st.offset = 100.0f;
    st.manual = true;

    // After following an index change, auto-centering is re-engaged.
    ui::strip_follow_index(st);
    CHECK_EQ(st.offset, 0.0f);
    CHECK_EQ(st.manual, false);
}

TEST(strip_wheel_marks_manual)
{
    ui::StripScrollState st;
    const float content = 500.0f;
    const float extent = 100.0f;

    CHECK_EQ(st.manual, false);
    ui::strip_apply_wheel(st, -1.0f, 10.0f, extent, content);
    CHECK_EQ(st.manual, true);
}

TEST(strip_content_extent_formula)
{
    // N thumbs, (N-1) gaps: content = N*thumb + (N-1)*gap.
    CHECK_EQ(ui::strip_content_extent(0, 30.0f, 10.0f), 0.0f);
    CHECK_EQ(ui::strip_content_extent(1, 30.0f, 10.0f), 30.0f);
    CHECK_EQ(ui::strip_content_extent(5, 30.0f, 10.0f), 5.0f * 30.0f + 4.0f * 10.0f);
}
