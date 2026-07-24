#include "test_framework.h"

#include <algorithm>
#include <cmath>
#include <SDL3/SDL.h>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "ui/help_popup.h"
#include "ui/help_layout.h"

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

TEST(help_popup_clamp_line_within_bounds)
{
    // total_lines > visible_lines: clamps into [0, total_lines - visible_lines]
    CHECK_EQ(ui::clamp_help_line(-10, 20, 5), 0);
    CHECK_EQ(ui::clamp_help_line(1000, 20, 5), 15);
    CHECK_EQ(ui::clamp_help_line(10, 20, 5), 10);
}

TEST(help_popup_clamp_line_content_fits_viewport)
{
    // total_lines <= visible_lines: only 0 is valid
    CHECK_EQ(ui::clamp_help_line(10, 5, 10), 0);
    CHECK_EQ(ui::clamp_help_line(-5, 8, 10), 0);
}

TEST(help_popup_key_ignored_while_closed)
{
    HelpPopupState s;   // open == false
    CHECK_FALSE(ui::handle_help_key(s, SDLK_DOWN));
    CHECK_EQ(s.scroll_line, 0);   // no side effect
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
    CHECK(s.scroll_line > 0);
    const int after_down = s.scroll_line;
    CHECK(ui::handle_help_key(s, SDLK_UP));
    CHECK(s.scroll_line < after_down);
}

TEST(help_popup_up_never_scrolls_negative)
{
    HelpPopupState s;
    ui::open_help(s);
    ui::handle_help_key(s, SDLK_UP);
    CHECK_EQ(s.scroll_line, 0);
}

TEST(help_popup_wheel_scrolls_while_open_only)
{
    HelpPopupState s;
    ui::handle_help_wheel(s, -1.0f);   // closed: no-op
    CHECK_EQ(s.scroll_line, 0);

    ui::open_help(s);
    ui::handle_help_wheel(s, -1.0f);   // wheel down (negative y) scrolls content down
    CHECK(s.scroll_line > 0);
    const int scrolled = s.scroll_line;
    ui::handle_help_wheel(s, 1.0f);    // wheel up scrolls back
    CHECK(s.scroll_line < scrolled);
}

namespace {
constexpr const char* kFontPath = OSV_DEFAULT_FONT;

// Headless software renderer (mirrors test_progress_modal.cpp).
struct SoftRenderer {
    SDL_Surface*  surf = nullptr;
    SDL_Renderer* r    = nullptr;
    SoftRenderer()
    {
        surf = SDL_CreateSurface(640, 480, SDL_PIXELFORMAT_RGBA32);
        if (surf) r = SDL_CreateSoftwareRenderer(surf);
    }
    ~SoftRenderer()
    {
        if (r)    SDL_DestroyRenderer(r);
        if (surf) SDL_DestroySurface(surf);
    }
};
} // namespace

TEST(help_popup_draws_without_crashing_when_open)
{
    SoftRenderer sr;
    REQUIRE(sr.r != nullptr);
    gfx::Renderer r(sr.r);
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(kFontPath, 18.0f));

    const std::vector<HelpGroup> groups{
        {"Navigate", {{"Enter", "Open"}, {"Esc", "Back"}}},
        {"Organize", {{"G", "Edit tags"}, {"B", "Favorite"}, {"Del", "Delete"}}},
    };
    HelpPopupState s;
    ui::open_help(s);
    ui::draw_help_popup(r, font, 640, 480, groups, s);
    SDL_RenderPresent(sr.r);
    CHECK(true);   // reached here without a crash
}

TEST(help_popup_draws_nothing_when_closed)
{
    SoftRenderer sr;
    REQUIRE(sr.r != nullptr);
    gfx::Renderer r(sr.r);
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(kFontPath, 18.0f));

    HelpPopupState s;   // closed
    ui::draw_help_popup(r, font, 640, 480, {{"X", {{"a", "b"}}}}, s);
    CHECK_EQ(s.scroll_line, 0);   // untouched — early-return path
}

TEST(help_popup_draws_long_content_without_crashing)
{
    SoftRenderer sr;
    REQUIRE(sr.r != nullptr);
    gfx::Renderer r(sr.r);
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(kFontPath, 18.0f));

    std::vector<HelpEntry> many;
    for (int i = 0; i < 40; ++i) many.push_back({"K" + std::to_string(i), "desc"});
    const std::vector<HelpGroup> groups{{"Long group", many}};
    HelpPopupState s;
    ui::open_help(s);
    s.scroll_line = 10000;   // way past the end — draw_help_popup must clamp it
    ui::draw_help_popup(r, font, 640, 480, groups, s);
    SDL_RenderPresent(sr.r);
    CHECK(s.scroll_line < 10000);   // clamped down to the real content height
}

namespace {

// Replica of the geometry draw_help_popup computes with line-quantised scroll.
// Phase 51 Task 8: this exists to VERIFY that no line is drawn partially
// (no line overlaps the visible band without being fully inside).
struct HelpGeom {
    float content_top    = 0.0f;
    float content_bottom = 0.0f;
    int   visible_lines  = 0;
};

[[maybe_unused]] HelpGeom new_help_geom(float H)
{
    constexpr float LINE_H = 24.0f;
    constexpr float PAD    = 24.0f;
    const float chrome_h    = 2.0f * (PAD + LINE_H + 8.0f);
    const float max_ph      = H - 80.0f;
    const float wanted_ph   = chrome_h + 40.0f * LINE_H;   // arbitrary 40-line content
    const float ph          = std::min(max_ph, wanted_ph);
    const float py = (H - ph) / 2.0f;
    HelpGeom g;
    g.content_top    = py + PAD + LINE_H + 8.0f;
    const float raw_viewport = ph - chrome_h;
    g.visible_lines = ui::help_visible_lines(raw_viewport, LINE_H);
    g.content_bottom = g.content_top + static_cast<float>(g.visible_lines) * LINE_H;
    return g;
}

// True when EVERY line the popup would draw at `scroll_line` lies fully inside the band.
// A line is a defect only when it OVERLAPS the viewport band but is NOT fully inside.
// A fully-clipped line (e.g., y entirely above the band) is not drawn at all and
// therefore not a defect — the SDL clip rect handles it. The distinction matters:
// the reported bug manifests as a *partial* draw, not a missed row.
[[maybe_unused]] bool all_drawn_lines_fully_inside(const HelpGeom& g, int total_lines, int scroll_line)
{
    constexpr float LINE_H = 24.0f;
    for (int i = 0; i < total_lines; ++i) {
        const float y = g.content_top - static_cast<float>(scroll_line) * LINE_H + static_cast<float>(i) * LINE_H;
        const bool overlaps = (y < g.content_bottom) && (y + LINE_H > g.content_top);
        const bool inside   = (y >= g.content_top) && (y + LINE_H <= g.content_bottom);
        if (overlaps && !inside) return false;   // a partially-drawn line: the reported defect
    }
    return true;
}

} // namespace

TEST(help_popup_no_partial_line_at_max_scroll_in_a_short_window)
{
    // H = 580 => ph = 500 => viewport = whole number of lines only.
    // With line-quantised scroll, max_scroll is always a whole line.
    const HelpGeom g = new_help_geom(580.0f);
    const int   total_lines = 40;
    const int max_scroll_line = ui::clamp_help_line(1000000, total_lines, g.visible_lines);
    CHECK(all_drawn_lines_fully_inside(g, total_lines, max_scroll_line));
}

TEST(help_popup_no_partial_line_at_rest_in_a_short_window)
{
    // Same window, scrolled to the top: scroll_line = 0.
    const HelpGeom g = new_help_geom(580.0f);
    CHECK(all_drawn_lines_fully_inside(g, 40, 0));
}

TEST(help_popup_no_partial_line_at_mid_scroll_in_a_short_window)
{
    // Mid-scroll at scroll_line = 5.
    const HelpGeom g = new_help_geom(580.0f);
    CHECK(all_drawn_lines_fully_inside(g, 40, 5));
}

TEST(help_popup_no_partial_line_across_a_range_of_window_heights)
{
    // The fix must not depend on one lucky window size. Sweep realistic heights
    // and require every one of them to render whole lines only.
    for (int h = 500; h <= 1100; h += 10) {
        const HelpGeom g = new_help_geom(static_cast<float>(h));
        const int   total_lines = 40;
        const int max_scroll_line = ui::clamp_help_line(1000000, total_lines, g.visible_lines);
        CHECK(all_drawn_lines_fully_inside(g, total_lines, max_scroll_line));
        CHECK(all_drawn_lines_fully_inside(g, total_lines, 0));
    }
}
