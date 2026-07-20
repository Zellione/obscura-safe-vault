#pragma once

#include <cstddef>

namespace ui {

// Hover budget (Phase 47): over-budget GIFs stay static.
inline constexpr int    kGifHoverMaxWidth  = 1920;
inline constexpr int    kGifHoverMaxHeight = 1080;
inline constexpr size_t kGifHoverMaxFrames = 300;
inline constexpr double kGifHoverDwell     = 0.200;   // seconds

// Matches media::kMinFrameDelay; duplicated here so this header stays free of
// the OSV_VENDORED_AV-gated media headers.
inline constexpr double kGifMinDelay = 0.02;

// Upper bound on frames skipped in one update, so a long stall (a modal, a
// slow decode) can never turn into an unbounded catch-up loop.
inline constexpr int kGifMaxCatchUpFrames = 64;

[[nodiscard]] bool gif_within_hover_budget(int width, int height, size_t frames) noexcept;

// Accumulates hover time on one tile and reports when animation should start.
class GifHoverGate {
public:
    // `tile` is the hovered tile's stable id, or -1 for "no tile hovered".
    // Returns true once the cursor has dwelled on `tile` for kGifHoverDwell.
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
[[nodiscard]] int gif_frames_to_advance(double& accumulator, double dt,
                                        double current_frame_delay, bool paused) noexcept;

}
