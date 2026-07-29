#pragma once

namespace ui {

// Pure input-silence debounce: arm() on every edit; fire(dt) returns true
// exactly once, delay_s after the LAST arm. cancel()+run-now for paths that
// must not act on stale results (e.g. Enter opening a result).
struct Debounce {
    double delay_s = 0.15;

    void arm() noexcept { pending_ = true; elapsed_ = 0.0; }
    void cancel() noexcept { pending_ = false; }
    [[nodiscard]] bool armed() const noexcept { return pending_; }
    [[nodiscard]] bool fire(double dt) noexcept
    {
        if (!pending_) return false;
        elapsed_ += dt;
        if (elapsed_ < delay_s) return false;
        pending_ = false;
        return true;
    }

private:
    bool   pending_ = false;
    double elapsed_ = 0.0;
};

} // namespace ui
