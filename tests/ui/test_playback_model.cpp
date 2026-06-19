#include "test_framework.h"

#include "ui/playback_model.h"

#include <cmath>
#include <string>

using namespace ui;

namespace {
bool close(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }
}  // namespace

// --- pure helpers ---------------------------------------------------------

TEST(playback_clamp_time)
{
    CHECK(close(clamp_time(-1.0, 10.0), 0.0));
    CHECK(close(clamp_time(5.0, 10.0), 5.0));
    CHECK(close(clamp_time(20.0, 10.0), 10.0));
    CHECK(close(clamp_time(5.0, 0.0), 0.0));   // degenerate duration
}

TEST(playback_seek_target)
{
    CHECK(close(seek_target(5.0, 5.0, 10.0), 10.0));   // exact end
    CHECK(close(seek_target(8.0, 5.0, 10.0), 10.0));   // clamp high
    CHECK(close(seek_target(3.0, -5.0, 10.0), 0.0));   // clamp low
    CHECK(close(seek_target(5.0, -2.0, 10.0), 3.0));
}

TEST(playback_bar_mapping)
{
    constexpr float bx = 100.0f, bw = 200.0f;
    constexpr double dur = 10.0;

    CHECK(close(time_to_bar_x(0.0, dur, bx, bw), 100.0));
    CHECK(close(time_to_bar_x(10.0, dur, bx, bw), 300.0));
    CHECK(close(time_to_bar_x(5.0, dur, bx, bw), 200.0));

    CHECK(close(bar_x_to_time(100.0f, dur, bx, bw), 0.0));
    CHECK(close(bar_x_to_time(300.0f, dur, bx, bw), 10.0));
    CHECK(close(bar_x_to_time(200.0f, dur, bx, bw), 5.0));

    // out-of-bar x clamps to [0, dur]
    CHECK(close(bar_x_to_time(50.0f, dur, bx, bw), 0.0));
    CHECK(close(bar_x_to_time(400.0f, dur, bx, bw), 10.0));

    // round trip
    const double t = 3.75;
    CHECK(close(bar_x_to_time(time_to_bar_x(t, dur, bx, bw), dur, bx, bw), t));
}

TEST(playback_bar_mapping_zero_duration)
{
    CHECK(close(time_to_bar_x(5.0, 0.0, 100.0f, 200.0f), 100.0));   // pinned at bar start
    CHECK(close(bar_x_to_time(250.0f, 0.0, 100.0f, 200.0f), 0.0));
}

TEST(playback_format_clock)
{
    CHECK(format_clock(0.0) == "0:00");
    CHECK(format_clock(5.0) == "0:05");
    CHECK(format_clock(65.0) == "1:05");
    CHECK(format_clock(600.0) == "10:00");
    CHECK(format_clock(3599.0) == "59:59");
    CHECK(format_clock(3600.0) == "1:00:00");
    CHECK(format_clock(3661.0) == "1:01:01");
    CHECK(format_clock(-4.0) == "0:00");
}

TEST(playback_frame_due)
{
    CHECK(PlaybackModel::frame_due(5.0, 5.0));
    CHECK(PlaybackModel::frame_due(5.0, 4.9));
    CHECK_FALSE(PlaybackModel::frame_due(5.0, 5.001));
}

// --- transport state machine ----------------------------------------------

TEST(playback_initial_state)
{
    PlaybackModel m(10.0);
    CHECK(close(m.position(), 0.0));
    CHECK(close(m.duration(), 10.0));
    CHECK_FALSE(m.playing());
    CHECK_FALSE(m.at_end());
}

TEST(playback_tick_only_while_playing)
{
    PlaybackModel m(10.0);
    m.tick(2.0);
    CHECK(close(m.position(), 0.0));   // paused: no advance

    m.set_playing(true);
    m.tick(2.0);
    CHECK(close(m.position(), 2.0));
    CHECK(m.playing());
}

TEST(playback_pauses_and_clamps_at_end)
{
    PlaybackModel m(10.0);
    m.set_playing(true);
    m.tick(20.0);
    CHECK(close(m.position(), 10.0));
    CHECK_FALSE(m.playing());   // auto-pause at end
    CHECK(m.at_end());
}

TEST(playback_seek_clamps)
{
    PlaybackModel m(10.0);
    m.seek_to(5.0);
    CHECK(close(m.position(), 5.0));
    m.seek_to(15.0);
    CHECK(close(m.position(), 10.0));
    m.seek_to(-3.0);
    CHECK(close(m.position(), 0.0));

    m.seek_to(5.0);
    m.seek_by(3.0);
    CHECK(close(m.position(), 8.0));
    m.seek_by(10.0);
    CHECK(close(m.position(), 10.0));
}

TEST(playback_toggle)
{
    PlaybackModel m(10.0);
    m.toggle();
    CHECK(m.playing());
    m.toggle();
    CHECK_FALSE(m.playing());
}
