#include "test_framework.h"

#include "media/av_sync.h"

#include <cmath>

using namespace media;

namespace {
bool close(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }
bool close_float(float a, float b, float eps = 1e-6f) { return std::fabs(a - b) <= eps; }
}  // namespace

TEST(decide_on_time_frame_is_presented)
{
    REQUIRE(decide(1.000, 1.000) == FrameAction::Present);
    REQUIRE(decide(1.000, 1.005) == FrameAction::Present);   // within ahead window
    REQUIRE(decide(1.000, 0.970) == FrameAction::Present);   // within behind window
}

TEST(decide_frame_far_ahead_of_clock_is_held)
{
    REQUIRE(decide(1.000, 1.100) == FrameAction::Hold);
}

TEST(decide_frame_far_behind_clock_is_dropped)
{
    REQUIRE(decide(1.000, 0.800) == FrameAction::Drop);
}

TEST(decide_drift_sequence_ahead_then_behind)
{
    // clock steady at 2.0; PTS sweeping past it
    REQUIRE(decide(2.0, 2.5) == FrameAction::Hold);
    REQUIRE(decide(2.0, 2.0) == FrameAction::Present);
    REQUIRE(decide(2.0, 1.5) == FrameAction::Drop);
}

TEST(audio_clock_base_offset_plus_consumed_samples)
{
    CHECK(close(audio_clock(0.0, 44100, 44100), 1.0));
    CHECK(close(audio_clock(5.0, 22050, 44100), 5.5));
    CHECK(close(audio_clock(3.0, 0, 0), 3.0));   // rate 0 -> just base
}

TEST(clamp_volume_bounds_to_zero_one)
{
    CHECK(close_float(clamp_volume(-0.5f), 0.0f));
    CHECK(close_float(clamp_volume(0.5f), 0.5f));
    CHECK(close_float(clamp_volume(2.0f), 1.0f));
}

TEST(effective_gain_respects_mute)
{
    CHECK(close_float(effective_gain(0.7f, false), 0.7f));
    CHECK(close_float(effective_gain(0.7f, true), 0.0f));
    CHECK(close_float(effective_gain(2.0f, false), 1.0f));   // clamped
}

TEST(audio_clock_no_drift_over_multi_hour_playback)
{
    // Phase 40 hardening: audio_clock() is a pure base + consumed/rate formula
    // (no iteratively-accumulated float state to drift), but a long loop-heavy
    // session can drive samples_consumed into the hundreds of millions —
    // confirm double precision holds exactly at that scale (well under 2^53,
    // so no representability loss) and decide() still classifies correctly
    // against a clock that large.
    constexpr int      kSampleRate = 48000;
    constexpr uint64_t kTwoHours   = static_cast<uint64_t>(kSampleRate) * 3600 * 2;

    const double clock_at_two_hours = audio_clock(0.0, kTwoHours, kSampleRate);
    CHECK(close(clock_at_two_hours, 7200.0));

    // A frame exactly on-time, one just ahead, and one just behind, all still
    // resolve correctly at this scale (no precision-induced misclassification).
    CHECK(decide(clock_at_two_hours, clock_at_two_hours) == FrameAction::Present);
    CHECK(decide(clock_at_two_hours, clock_at_two_hours + 0.100) == FrameAction::Hold);
    CHECK(decide(clock_at_two_hours, clock_at_two_hours - 0.100) == FrameAction::Drop);

    // Simulate a loop replaying the same ~1s clip ~7200 times (a two-hour
    // session's worth of loop-boundary reseeks, each resetting base_seconds
    // to 0 like do_seek(0.0) does) — the per-loop clock must stay exact, not
    // accumulate error the way repeatedly adding a small float delta would.
    for (int loop = 0; loop < 7200; ++loop) {
        const double c = audio_clock(0.0, static_cast<uint64_t>(kSampleRate) / 2, kSampleRate);
        CHECK(close(c, 0.5));
    }
}

TEST(audio_seek_skip_drops_frames_ending_before_target)
{
    // 1024 frames @ 48k starting at 9.0s end at ~9.021s — before a 10.0s target.
    CHECK(audio_seek_skip(9.0, 1024, 48000, 10.0) == AudioSeekSkip::Drop);
}

TEST(audio_seek_skip_starts_on_frame_straddling_target)
{
    // Frame spans [9.99, 10.011) around a 10.0s target.
    CHECK(audio_seek_skip(9.99, 1024, 48000, 10.0) == AudioSeekSkip::Start);
}

TEST(audio_seek_skip_frame_ending_exactly_at_target_is_dropped)
{
    // [9.9787.., 10.0) contains nothing at/after the target.
    CHECK(audio_seek_skip(10.0 - 1024.0 / 48000.0, 1024, 48000, 10.0)
          == AudioSeekSkip::Drop);
}

TEST(audio_seek_skip_frame_at_or_after_target_starts)
{
    CHECK(audio_seek_skip(10.0, 1024, 48000, 10.0) == AudioSeekSkip::Start);
    CHECK(audio_seek_skip(12.5, 1024, 48000, 10.0) == AudioSeekSkip::Start);
}

TEST(audio_seek_skip_fails_open_on_bad_rate_or_empty_frame)
{
    // Never drop audio forever on degenerate input.
    CHECK(audio_seek_skip(9.0, 1024, 0, 10.0) == AudioSeekSkip::Start);
    CHECK(audio_seek_skip(9.0, 0, 48000, 10.0) == AudioSeekSkip::Start);
}

TEST(audio_seek_skip_seek_to_zero_keeps_first_frame)
{
    CHECK(audio_seek_skip(0.0, 1024, 48000, 0.0) == AudioSeekSkip::Start);
}
