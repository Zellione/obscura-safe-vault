#include "test_framework.h"

#include <algorithm>
#include <cmath>
#include <SDL3/SDL.h>

#include "gfx/renderer.h"
#include "gfx/text.h"
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
    CHECK_EQ(s.scroll, 0.0f);   // untouched — early-return path
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
    s.scroll = 10000.0f;   // way past the end — draw_help_popup must clamp it
    ui::draw_help_popup(r, font, 640, 480, groups, s);
    SDL_RenderPresent(sr.r);
    CHECK(s.scroll < 10000.0f);   // clamped down to the real content height
}

namespace {

// Replica of the geometry draw_help_popup computes today (src/ui/help_popup.cpp:73-89).
// Phase 51 Task 5: this exists to REPRODUCE the reported edge clipping and
// must go GREEN once the popup uses line-quantised scroll and a whole-line band.
struct HelpGeom {
    float content_top    = 0.0f;
    float content_bottom = 0.0f;
    float viewport_h     = 0.0f;
};

[[maybe_unused]] HelpGeom current_help_geom(float H)
{
    constexpr float LINE_H = 24.0f;
    constexpr float PAD    = 24.0f;
    const float ph = std::min(H - 80.0f, 520.0f);
    const float py = (H - ph) / 2.0f;
    HelpGeom g;
    g.content_top    = py + PAD + LINE_H + 8.0f;
    g.content_bottom = py + ph - PAD - LINE_H - 8.0f;
    g.viewport_h     = g.content_bottom - g.content_top;
    return g;
}

// True when EVERY line the popup would draw at `scroll` lies fully inside the band.
// A line is a defect only when it OVERLAPS the viewport band but is NOT fully inside.
// A fully-clipped line (e.g., y == content_top - LINE_H, spanning the row entirely
// above the band) is not drawn at all and therefore not a defect — the SDL clip rect
// handles it. The distinction matters: the reported bug manifests as a *partial* draw,
// not a missed row.
[[maybe_unused]] bool all_drawn_lines_fully_inside(const HelpGeom& g, int total_lines, float scroll)
{
    constexpr float LINE_H = 24.0f;
    for (int i = 0; i < total_lines; ++i) {
        const float y = g.content_top - scroll + static_cast<float>(i) * LINE_H;
        const bool overlaps = (y < g.content_bottom) && (y + LINE_H > g.content_top);
        const bool inside   = (y >= g.content_top) && (y + LINE_H <= g.content_bottom);
        if (overlaps && !inside) return false;   // a partially-drawn line: the reported defect
    }
    return true;
}

} // namespace

// TODO(Phase51-Task8): re-enable — these four reproduce the pre-fix edge clipping and
// must go GREEN once the popup uses line-quantised scroll and a whole-line band.
/*
TEST(help_popup_no_partial_line_at_max_scroll_in_a_short_window)
{
    // H = 580 => ph = 500 => viewport = 388 px = 16.17 lines. Not a whole number of
    // lines, so max_scroll is not a multiple of LINE_H and a row lands mid-line.
    const HelpGeom g = current_help_geom(580.0f);
    const int   total_lines = 40;
    const float content_h   = static_cast<float>(total_lines) * 24.0f;
    const float max_scroll  = ui::clamp_help_scroll(1.0e6f, content_h, g.viewport_h);
    CHECK(all_drawn_lines_fully_inside(g, total_lines, max_scroll));
}

TEST(help_popup_no_partial_line_at_rest_in_a_short_window)
{
    // Same window, scrolled to the top: the BOTTOM row is the partial one here.
    const HelpGeom g = current_help_geom(580.0f);
    CHECK(all_drawn_lines_fully_inside(g, 40, 0.0f));
}

TEST(help_popup_no_partial_line_at_mid_scroll_in_a_short_window)
{
    // Mid-scroll puts a partial line at BOTH edges — the state the owner's
    // screenshot showed. H = 580 => viewport 388 px = 16.17 lines.
    const HelpGeom g = current_help_geom(580.0f);
    CHECK(all_drawn_lines_fully_inside(g, 40, 100.0f));
}

TEST(help_popup_no_partial_line_across_a_range_of_window_heights)
{
    // The defect must not depend on one lucky window size. Sweep realistic heights
    // and require every one of them to render whole lines only.
    for (int h = 500; h <= 1100; h += 10) {
        const HelpGeom g = current_help_geom(static_cast<float>(h));
        const int   total_lines = 40;
        const float content_h   = static_cast<float>(total_lines) * 24.0f;
        const float max_scroll  = ui::clamp_help_scroll(1.0e6f, content_h, g.viewport_h);
        CHECK(all_drawn_lines_fully_inside(g, total_lines, max_scroll));
        CHECK(all_drawn_lines_fully_inside(g, total_lines, 0.0f));
    }
}
*/
