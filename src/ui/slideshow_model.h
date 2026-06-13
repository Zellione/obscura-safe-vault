#pragma once

#include <cstdint>
#include <random>
#include <vector>

// Pure state machine for the image viewer's slideshow mode (Phase 11). Like
// viewer_model / scroll_model it is deliberately SDL-, GPU- and IO-free so the
// timing, wrap/loop, shuffle and cross-fade maths can be unit-tested headlessly;
// the ImageViewer screen owns the SDL plumbing (decode, textures, rendering) and
// drives this model from its update(dt) hook.
//
// Time model: tick(dt) accumulates elapsed time while running(); each time the
// accumulator crosses dwell() the current image advances by one (wrapping at the
// gallery end) and a cross-fade begins. fade_progress() ramps 0 -> 1 over
// SLIDESHOW_FADE seconds; prev_index() names the outgoing image while the fade is
// in flight (and is -1 otherwise).
namespace ui {

inline constexpr double SLIDESHOW_DWELL_MIN     = 1.0;   // seconds per image
inline constexpr double SLIDESHOW_DWELL_MAX     = 30.0;
inline constexpr double SLIDESHOW_DWELL_DEFAULT = 4.0;
inline constexpr double SLIDESHOW_DWELL_STEP    = 1.0;   // [ / ] adjustment
inline constexpr double SLIDESHOW_FADE          = 0.5;   // cross-fade duration

// Clamp a dwell time into [SLIDESHOW_DWELL_MIN, SLIDESHOW_DWELL_MAX].
[[nodiscard]] double clamp_dwell(double dwell) noexcept;

class SlideshowModel {
public:
    // `start_index` is the first image shown; the slideshow starts running. With
    // `shuffle`, each cycle visits every image exactly once in a `seed`-determined
    // random order (the first image shown is still `start_index`).
    SlideshowModel(int count, int start_index, double dwell = SLIDESHOW_DWELL_DEFAULT,
                   bool shuffle = false, uint64_t seed = 0);

    [[nodiscard]] int    count() const noexcept { return count_; }
    [[nodiscard]] int    index() const noexcept { return cur_; }       // current image
    [[nodiscard]] int    prev_index() const noexcept;                  // outgoing, or -1
    [[nodiscard]] bool   running() const noexcept { return running_; }
    [[nodiscard]] double dwell() const noexcept { return dwell_; }

    // Cross-fade progress of the in-flight transition, in [0, 1]; 1 when no
    // transition is active (the current image is fully shown).
    [[nodiscard]] double fade_progress() const noexcept;

    void set_running(bool on) noexcept { running_ = on; }
    void toggle() noexcept { running_ = !running_; }
    void set_dwell(double dwell) noexcept { dwell_ = clamp_dwell(dwell); }
    void adjust_dwell(double delta) noexcept { set_dwell(dwell_ + delta); }

    // Manually step by `delta` images (wraps); begins a cross-fade and resets the
    // dwell accumulator. Used by the next/prev keys and internally by tick().
    void advance(int delta) noexcept;

    // Advance time by `dt` seconds. Progresses any in-flight cross-fade always,
    // but only accumulates toward the next auto-advance while running(). Returns
    // true if the current image changed this tick.
    bool tick(double dt) noexcept;

private:
    void step(int delta) noexcept;   // change cur_ without touching the fade/timer

    int    count_   = 0;
    int    cur_     = 0;
    int    prev_    = -1;            // outgoing image during a fade, else -1
    bool   running_ = true;
    double dwell_   = SLIDESHOW_DWELL_DEFAULT;
    double elapsed_ = 0.0;          // toward the next auto-advance
    double fade_    = SLIDESHOW_FADE; // elapsed fade time (>= SLIDESHOW_FADE = done)

    bool                 shuffle_ = false;
    std::vector<int>     order_;    // permutation when shuffling
    int                  pos_ = 0;  // position within order_
    std::mt19937_64      rng_;
};

} // namespace ui
