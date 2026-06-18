#pragma once

namespace app {

// Tracks elapsed idle time and fires once when a fixed timeout is crossed.
// Pure (no SDL); unit-tested. The owner resets it on user input and ticks it
// each frame only while there is something to lock.
class IdleTimer {
public:
    explicit constexpr IdleTimer(double timeout_secs) noexcept : timeout_(timeout_secs) {}

    void reset() noexcept { elapsed_ = 0.0; }

    // Advance by dt; returns true exactly on the tick that first reaches the
    // timeout (and resets), so the caller fires its action once per crossing.
    [[nodiscard]] bool tick(double dt) noexcept
    {
        elapsed_ += dt;
        if (elapsed_ >= timeout_) {
            elapsed_ = 0.0;
            return true;
        }
        return false;
    }

    [[nodiscard]] double elapsed() const noexcept { return elapsed_; }
    [[nodiscard]] double timeout() const noexcept { return timeout_; }

private:
    double timeout_;
    double elapsed_ = 0.0;
};

} // namespace app
