#include "test_framework.h"

#include "ui/gif_model.h"

TEST(gif_hover_budget_accepts_a_small_gif)
{
    CHECK(ui::gif_within_hover_budget(320, 240, 12));
}

TEST(gif_hover_budget_accepts_the_exact_limits)
{
    CHECK(ui::gif_within_hover_budget(1920, 1080, 300));
}

TEST(gif_hover_budget_rejects_oversized_dimensions)
{
    CHECK(!ui::gif_within_hover_budget(1921, 1080, 10));
    CHECK(!ui::gif_within_hover_budget(1920, 1081, 10));
}

TEST(gif_hover_budget_rejects_too_many_frames)
{
    CHECK(!ui::gif_within_hover_budget(320, 240, 301));
}

TEST(gif_hover_budget_rejects_degenerate_dimensions)
{
    CHECK(!ui::gif_within_hover_budget(0, 240, 10));
    CHECK(!ui::gif_within_hover_budget(320, 0, 10));
}

TEST(gif_hover_gate_waits_for_the_dwell)
{
    ui::GifHoverGate g;
    CHECK(!g.update(7, 0.100));
    CHECK_EQ(g.active_tile(), -1);
    CHECK(g.update(7, 0.150));          // 0.250s total, past the 0.200s dwell
    CHECK_EQ(g.active_tile(), 7);
}

TEST(gif_hover_gate_fires_only_once_per_tile)
{
    ui::GifHoverGate g;
    CHECK(g.update(7, 0.300));
    CHECK(!g.update(7, 0.300));         // already active, no second start
    CHECK_EQ(g.active_tile(), 7);
}

TEST(gif_hover_gate_resets_when_the_tile_changes)
{
    ui::GifHoverGate g;
    CHECK(!g.update(7, 0.150));
    CHECK(!g.update(8, 0.150));         // dwell restarted on the new tile
    CHECK_EQ(g.active_tile(), -1);
    CHECK(g.update(8, 0.100));
    CHECK_EQ(g.active_tile(), 8);
}

TEST(gif_hover_gate_resets_when_the_cursor_leaves)
{
    ui::GifHoverGate g;
    CHECK(g.update(7, 0.300));
    CHECK(!g.update(-1, 0.016));
    CHECK_EQ(g.active_tile(), -1);
}

TEST(gif_advance_holds_within_one_frame_delay)
{
    double acc = 0.0;
    CHECK_EQ(ui::gif_frames_to_advance(acc, 0.010, 0.100, false), 0);
}

TEST(gif_advance_steps_one_frame_past_the_delay)
{
    double acc = 0.0;
    CHECK_EQ(ui::gif_frames_to_advance(acc, 0.120, 0.100, false), 1);
}

TEST(gif_advance_catches_up_after_a_long_stall)
{
    double acc = 0.0;
    CHECK_EQ(ui::gif_frames_to_advance(acc, 0.350, 0.100, false), 3);
}

TEST(gif_advance_does_nothing_while_paused)
{
    double acc = 0.0;
    CHECK_EQ(ui::gif_frames_to_advance(acc, 5.000, 0.100, true), 0);
    CHECK_EQ(acc, 0.0);
}

TEST(gif_advance_tolerates_a_zero_delay)
{
    double acc = 0.0;
    const int n = ui::gif_frames_to_advance(acc, 0.100, 0.0, false);
    CHECK(n >= 1);
    CHECK(n <= 64);   // bounded, never an unbounded catch-up loop
}
