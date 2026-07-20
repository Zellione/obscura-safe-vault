#include "ui/gif_model.h"

namespace ui {

bool gif_within_hover_dimension_budget(int width, int height) noexcept
{
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (width > kGifHoverMaxWidth || height > kGifHoverMaxHeight) {
        return false;
    }
    return true;
}

bool gif_hover_frame_count_exceeded(size_t frames) noexcept
{
    return frames > kGifHoverMaxFrames;
}

bool gif_within_hover_budget(int width, int height, size_t frames) noexcept
{
    return gif_within_hover_dimension_budget(width, height) && !gif_hover_frame_count_exceeded(frames);
}

bool GifHoverGate::update(int tile, double dt) noexcept
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
    if (dwell_ < kGifHoverDwell) {
        return false;
    }

    active_ = tile_;
    return true;                    // exactly one "start animating" edge per tile
}

int gif_frames_to_advance(double& accumulator, double dt,
                          double current_frame_delay, bool paused) noexcept
{
    if (paused) {
        return 0;
    }

    const double delay = current_frame_delay >= kGifMinDelay ? current_frame_delay
                                                             : kGifMinDelay;
    accumulator += dt;

    int steps = 0;
    while (accumulator >= delay && steps < kGifMaxCatchUpFrames) {
        accumulator -= delay;
        ++steps;
    }
    if (steps == kGifMaxCatchUpFrames) {
        accumulator = 0.0;  // drop the rest of a long stall
    }
    return steps;
}

}  // namespace ui
