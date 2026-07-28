#pragma once

#include <cstddef>

namespace ui {

// Hover budget (Phase 47): over-budget animations stay static.
inline constexpr int    kAnimHoverMaxWidth  = 1920;
inline constexpr int    kAnimHoverMaxHeight = 1080;
inline constexpr size_t kAnimHoverMaxFrames = 300;
inline constexpr double kAnimHoverDwell     = 0.200;   // seconds

// Matches media::kMinFrameDelay; duplicated here so this header stays free of
// the OSV_VENDORED_AV-gated media headers.
inline constexpr double kAnimMinDelay = 0.02;

// Upper bound on frames skipped in one update, so a long stall (a modal, a
// slow decode) can never turn into an unbounded catch-up loop.
inline constexpr int kAnimMaxCatchUpFrames = 64;

// Separate predicates for dimension and frame count budgets.
// The dimension budget can be checked before decoding.
[[nodiscard]] bool anim_within_hover_dimension_budget(int width, int height) noexcept;

// The frame count budget is checked during playback after decoding.
[[nodiscard]] bool anim_hover_frame_count_exceeded(size_t frames) noexcept;

// Accumulates hover time on one tile and reports when animation should start.
class AnimHoverGate {
public:
    // `tile` is the hovered tile's stable id, or -1 for "no tile hovered".
    // Returns true once the cursor has dwelled on `tile` for kAnimHoverDwell.
    bool update(int tile, double dt) noexcept;
    [[nodiscard]] int  active_tile() const noexcept { return active_; }
    void reset() noexcept { tile_ = -1; dwell_ = 0.0; active_ = -1; }

private:
    int    tile_   = -1;
    double dwell_  = 0.0;
    int    active_ = -1;
};

// Advances a looping frame clock. Returns the number of frames to step (0 or
// more); the caller pulls that many frames from the decoder.
[[nodiscard]] int anim_frames_to_advance(double& accumulator, double dt,
                                        double current_frame_delay, bool paused) noexcept;

}
