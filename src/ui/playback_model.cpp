#include "ui/playback_model.h"

#include <algorithm>
#include <format>

namespace ui {

double clamp_time(double t, double duration) noexcept
{
    if (duration <= 0.0) return 0.0;
    return std::clamp(t, 0.0, duration);
}

double seek_target(double current, double delta, double duration) noexcept
{
    return clamp_time(current + delta, duration);
}

float time_to_bar_x(double t, double duration, float bar_x, float bar_w) noexcept
{
    if (duration <= 0.0) return bar_x;
    const double frac = std::clamp(t / duration, 0.0, 1.0);
    return bar_x + static_cast<float>(frac) * bar_w;
}

double bar_x_to_time(float x, double duration, float bar_x, float bar_w) noexcept
{
    if (duration <= 0.0 || bar_w <= 0.0f) return 0.0;
    const double frac = std::clamp((x - bar_x) / bar_w, 0.0f, 1.0f);
    return frac * duration;
}

std::string format_clock(double seconds)
{
    if (!(seconds > 0.0)) return "0:00";   // also catches NaN
    const auto total = static_cast<long long>(seconds);
    const long long h = total / 3600;
    const long long m = (total % 3600) / 60;
    const long long s = total % 60;
    if (h > 0) return std::format("{}:{:02}:{:02}", h, m, s);
    return std::format("{}:{:02}", m, s);
}

bool PlaybackModel::at_end() const noexcept
{
    return duration_ <= 0.0 || position_ >= duration_ - 1e-9;
}

void PlaybackModel::tick(double dt) noexcept
{
    if (!playing_ || dt <= 0.0) return;
    position_ += dt;
    if (position_ >= duration_) {
        position_ = duration_;
        playing_  = false;   // auto-pause at the end
    }
}

}  // namespace ui
