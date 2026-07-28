#include "ui/anim_model.h"

namespace ui {

bool anim_within_hover_dimension_budget(int width, int height) noexcept
{
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (width > kAnimHoverMaxWidth || height > kAnimHoverMaxHeight) {
        return false;
    }
    return true;
}

bool anim_hover_frame_count_exceeded(size_t frames) noexcept
{
    return frames > kAnimHoverMaxFrames;
}

bool AnimHoverGate::update(int tile, double dt) noexcept
{
    if (tile != tile_) {            // cursor moved to another tile, or off the grid
        tile_   = tile;
        dwell_  = 0.0;
        active_ = -1;
    }
    if (tile_ < 0 || active_ == tile_) {
        return false;
    }

    dwell_ += dt;
    if (dwell_ < kAnimHoverDwell) {
        return false;
    }

    active_ = tile_;
    return true;                    // exactly one "start animating" edge per tile
}

int anim_frames_to_advance(double& accumulator, double dt,
                          double current_frame_delay, bool paused) noexcept
{
    if (paused) {
        return 0;
    }

    const double delay = current_frame_delay >= kAnimMinDelay ? current_frame_delay
                                                             : kAnimMinDelay;
    accumulator += dt;

    int steps = 0;
    while (accumulator >= delay && steps < kAnimMaxCatchUpFrames) {
        accumulator -= delay;
        ++steps;
    }
    if (steps == kAnimMaxCatchUpFrames) {
        accumulator = 0.0;  // drop the rest of a long stall
    }
    return steps;
}

}  // namespace ui
