#include "test_framework.h"

#include <string>
#include <string_view>

#include "ui/text_field_view.h"

namespace {

// Fixed-width stub measurer: every character is 10 px wide. Multi-byte UTF-8
// characters measure 10 px too (one character, one advance), which is what the
// real atlas does for any glyph it has.
int stub_measure(std::string_view s)
{
    int chars = 0;
    for (char c : s)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++chars;
    return chars * 10;
}

const ui::TextMeasureFn MEASURE = stub_measure;   // NOLINT(cert-err58-cpp)

} // namespace

TEST(tfv_short_text_needs_no_scroll)
{
    const auto L = ui::layout_text_field("abc", 3, 0, 0, 200.0f, 0.0f, MEASURE);
    CHECK_EQ(L.vis_begin, size_t{0});
    CHECK_EQ(L.vis_end, size_t{3});
    CHECK(L.scroll == 0.0f);
    CHECK(L.text_x == 0.0f);
    CHECK(L.caret_x == 30.0f);
    CHECK(L.caret_visible);
    CHECK(L.sel_w == 0.0f);
}

TEST(tfv_scroll_follows_the_caret_past_the_right_edge)
{
    // 10 characters (100 px) in a 50 px field, caret at the end.
    const auto L = ui::layout_text_field("abcdefghij", 10, 0, 0, 50.0f, 0.0f, MEASURE);
    CHECK(L.scroll == 50.0f);
    CHECK(L.caret_x == 50.0f);          // parked on the right edge
    CHECK(L.caret_visible);
    CHECK_EQ(L.vis_begin, size_t{5});   // "fghij"
    CHECK_EQ(L.vis_end, size_t{10});
    CHECK(L.text_x == 0.0f);
}

TEST(tfv_scroll_follows_the_caret_past_the_left_edge)
{
    // Carrying a scroll of 50 px, the caret moves back to offset 2.
    const auto L = ui::layout_text_field("abcdefghij", 2, 0, 0, 50.0f, 50.0f, MEASURE);
    CHECK(L.scroll == 20.0f);
    CHECK(L.caret_x == 0.0f);           // parked on the left edge
    CHECK(L.caret_visible);
    CHECK_EQ(L.vis_begin, size_t{2});
    CHECK_EQ(L.vis_end, size_t{7});
}

TEST(tfv_scroll_never_runs_past_the_end_of_the_text)
{
    // A stale, oversized scroll is clamped to total - field_w.
    const auto L = ui::layout_text_field("abcdefghij", 10, 0, 0, 50.0f, 900.0f, MEASURE);
    CHECK(L.scroll == 50.0f);
    CHECK_EQ(L.vis_end, size_t{10});
}

TEST(tfv_scroll_resets_to_zero_when_the_text_shrinks_to_fit)
{
    const auto L = ui::layout_text_field("ab", 2, 0, 0, 50.0f, 40.0f, MEASURE);
    CHECK(L.scroll == 0.0f);
    CHECK_EQ(L.vis_begin, size_t{0});
    CHECK_EQ(L.vis_end, size_t{2});
}

TEST(tfv_partially_visible_characters_are_elided_at_the_right_edge)
{
    // 45 px field: four whole characters fit, the fifth would overhang.
    const auto L = ui::layout_text_field("abcdefghij", 0, 0, 0, 45.0f, 0.0f, MEASURE);
    CHECK_EQ(L.vis_begin, size_t{0});
    CHECK_EQ(L.vis_end, size_t{4});
}

TEST(tfv_selection_rect_spans_the_selected_characters)
{
    const auto L = ui::layout_text_field("abcdefghij", 5, 2, 5, 200.0f, 0.0f, MEASURE);
    CHECK(L.sel_x == 20.0f);
    CHECK(L.sel_w == 30.0f);
}

TEST(tfv_selection_rect_clips_to_the_visible_region)
{
    // Selection [0,10) but only [50,100) of the text is on screen.
    const auto L = ui::layout_text_field("abcdefghij", 10, 0, 10, 50.0f, 0.0f, MEASURE);
    CHECK(L.scroll == 50.0f);
    CHECK(L.sel_x == 0.0f);
    CHECK(L.sel_w == 50.0f);            // clipped to the field, not 100 px
}

TEST(tfv_selection_entirely_off_screen_draws_nothing)
{
    // Selection [0,2) sits at 0..20 px, the view starts at 50 px.
    const auto L = ui::layout_text_field("abcdefghij", 10, 0, 2, 50.0f, 0.0f, MEASURE);
    CHECK(L.sel_w == 0.0f);
}

TEST(tfv_multibyte_text_measures_by_character)
{
    // "héllo": 6 bytes, 5 characters. The caret sits after 'é' (byte offset 3),
    // which is 2 characters in => 20 px, not 30.
    const auto L = ui::layout_text_field("h\xC3\xA9llo", 3, 0, 0, 200.0f, 0.0f, MEASURE);
    CHECK(L.caret_x == 20.0f);
    CHECK_EQ(L.vis_end, size_t{6});
}

TEST(tfv_empty_text_and_degenerate_field)
{
    const auto empty = ui::layout_text_field("", 0, 0, 0, 100.0f, 0.0f, MEASURE);
    CHECK_EQ(empty.vis_end, size_t{0});
    CHECK(empty.caret_x == 0.0f);
    CHECK(empty.caret_visible);

    // A collapsed field lays nothing out rather than dividing by zero.
    const auto none = ui::layout_text_field("abc", 3, 0, 0, 0.0f, 0.0f, MEASURE);
    CHECK_EQ(none.vis_end, size_t{0});
    CHECK(!none.caret_visible);
}

TEST(tfv_caret_is_solid_for_half_a_period_after_an_edit)
{
    constexpr uint64_t EDIT = 10'000;
    CHECK(ui::caret_is_on(EDIT, EDIT));            // the instant of the keystroke
    CHECK(ui::caret_is_on(EDIT + 499, EDIT));      // still solid
    CHECK(!ui::caret_is_on(EDIT + 500, EDIT));     // first off-phase
    CHECK(!ui::caret_is_on(EDIT + 999, EDIT));
    CHECK(ui::caret_is_on(EDIT + 1000, EDIT));     // back on
}

TEST(tfv_caret_blink_tolerates_a_clock_that_predates_the_last_edit)
{
    CHECK(ui::caret_is_on(5'000, 9'000));
}
