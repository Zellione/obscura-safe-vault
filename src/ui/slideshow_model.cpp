#include "ui/slideshow_model.h"

#include <algorithm>
#include <numeric>

#include "ui/viewer_model.h"   // wrap_index

namespace ui {

double clamp_dwell(double dwell) noexcept
{
    return std::clamp(dwell, SLIDESHOW_DWELL_MIN, SLIDESHOW_DWELL_MAX);
}

SlideshowModel::SlideshowModel(int count, int start_index, double dwell, bool shuffle,
                               uint64_t seed)
    : count_(std::max(0, count)),
      dwell_(clamp_dwell(dwell)),
      shuffle_(shuffle),
      rng_(seed)
{
    cur_ = count_ > 0 ? std::clamp(start_index, 0, count_ - 1) : 0;
    if (shuffle_ && count_ > 0) {
        order_.resize(static_cast<size_t>(count_));
        std::iota(order_.begin(), order_.end(), 0);
        std::ranges::shuffle(order_, rng_);
        // Pin the requested start image to the front so it is shown first; the
        // rest of this cycle still visits every other image exactly once.
        const auto it = std::ranges::find(order_, cur_);
        std::iter_swap(order_.begin(), it);
        pos_ = 0;
    }
}

int SlideshowModel::prev_index() const noexcept
{
    return fade_ < SLIDESHOW_FADE ? prev_ : -1;
}

double SlideshowModel::fade_progress() const noexcept
{
    if (fade_ >= SLIDESHOW_FADE) return 1.0;
    return std::clamp(fade_ / SLIDESHOW_FADE, 0.0, 1.0);
}

void SlideshowModel::step(int delta) noexcept
{
    if (count_ <= 0) return;
    if (!shuffle_) {
        cur_ = wrap_index(cur_, delta, count_);
        return;
    }
    int np = pos_ + delta;
    if (np < 0 || np >= count_) {                 // crossed a cycle boundary
        std::ranges::shuffle(order_, rng_);
        np = ((np % count_) + count_) % count_;
    }
    pos_ = np;
    cur_ = order_[static_cast<size_t>(pos_)];
}

void SlideshowModel::advance(int delta) noexcept
{
    if (count_ <= 0) return;
    prev_    = cur_;
    step(delta);
    fade_    = 0.0;     // begin a fresh cross-fade
    elapsed_ = 0.0;     // restart the dwell timer from this image
}

bool SlideshowModel::tick(double dt) noexcept
{
    if (dt < 0.0) dt = 0.0;

    // The cross-fade always plays out, even if paused mid-transition, so the
    // incoming image settles fully.
    if (fade_ < SLIDESHOW_FADE) fade_ = std::min(fade_ + dt, SLIDESHOW_FADE);

    if (!running_ || count_ <= 0) return false;

    elapsed_ += dt;
    bool advanced = false;
    while (elapsed_ >= dwell_) {
        elapsed_ -= dwell_;
        prev_ = cur_;
        step(1);
        fade_    = 0.0;
        advanced = true;
    }
    return advanced;
}

} // namespace ui
