#include "test_framework.h"

#include <SDL3/SDL.h>

#include "ui/help_popup.h"

using ui::HelpEntry;
using ui::HelpGroup;
using ui::HelpPopupState;

TEST(help_popup_open_close_toggle)
{
    HelpPopupState s;
    CHECK_FALSE(s.open);
    ui::open_help(s);
    CHECK(s.open);
    ui::close_help(s);
    CHECK_FALSE(s.open);
    ui::toggle_help(s);
    CHECK(s.open);
    ui::toggle_help(s);
    CHECK_FALSE(s.open);
}

TEST(help_popup_line_count_counts_titles_entries_and_spacers)
{
    const std::vector<HelpGroup> groups{
        {"A", {{"x", "one"}, {"y", "two"}}},   // 1 title + 2 entries = 3
        {"B", {{"z", "three"}}},                // spacer(1) + 1 title + 1 entry = 3
    };
    CHECK_EQ(ui::help_line_count(groups), 6);
}

TEST(help_popup_line_count_empty_groups_is_zero)
{
    CHECK_EQ(ui::help_line_count({}), 0);
}

TEST(help_popup_clamp_scroll_within_bounds)
{
    // content taller than viewport: clamps into [0, content_h - viewport_h]
    CHECK_EQ(ui::clamp_help_scroll(-10.0f, 500.0f, 300.0f), 0.0f);
    CHECK_EQ(ui::clamp_help_scroll(1000.0f, 500.0f, 300.0f), 200.0f);
    CHECK_EQ(ui::clamp_help_scroll(50.0f, 500.0f, 300.0f), 50.0f);
}

TEST(help_popup_clamp_scroll_content_fits_viewport)
{
    // content shorter than (or equal to) viewport: only 0 is valid
    CHECK_EQ(ui::clamp_help_scroll(50.0f, 200.0f, 300.0f), 0.0f);
    CHECK_EQ(ui::clamp_help_scroll(-5.0f, 200.0f, 300.0f), 0.0f);
}

TEST(help_popup_key_ignored_while_closed)
{
    HelpPopupState s;   // open == false
    CHECK_FALSE(ui::handle_help_key(s, SDLK_DOWN));
    CHECK_EQ(s.scroll, 0.0f);   // no side effect
}

TEST(help_popup_escape_and_q_close_while_open)
{
    HelpPopupState s;
    ui::open_help(s);
    CHECK(ui::handle_help_key(s, SDLK_ESCAPE));
    CHECK_FALSE(s.open);

    ui::open_help(s);
    CHECK(ui::handle_help_key(s, SDLK_Q));
    CHECK_FALSE(s.open);
}

TEST(help_popup_up_down_scroll_while_open)
{
    HelpPopupState s;
    ui::open_help(s);
    CHECK(ui::handle_help_key(s, SDLK_DOWN));
    CHECK(s.scroll > 0.0f);
    const float after_down = s.scroll;
    CHECK(ui::handle_help_key(s, SDLK_UP));
    CHECK(s.scroll < after_down);
}

TEST(help_popup_up_never_scrolls_negative)
{
    HelpPopupState s;
    ui::open_help(s);
    ui::handle_help_key(s, SDLK_UP);
    CHECK_EQ(s.scroll, 0.0f);
}

TEST(help_popup_wheel_scrolls_while_open_only)
{
    HelpPopupState s;
    ui::handle_help_wheel(s, -1.0f);   // closed: no-op
    CHECK_EQ(s.scroll, 0.0f);

    ui::open_help(s);
    ui::handle_help_wheel(s, -1.0f);   // wheel down (negative y) scrolls content down
    CHECK(s.scroll > 0.0f);
    const float scrolled = s.scroll;
    ui::handle_help_wheel(s, 1.0f);    // wheel up scrolls back
    CHECK(s.scroll < scrolled);
}
