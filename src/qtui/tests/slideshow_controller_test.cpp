#include <cstdio>
#include <cmath>
#include "ui/slideshow_model.h"

// Unit test for slideshow model behavior (SDL-free)
// Tests: initialization, advance/wrap, dwell adjustment, shuffle sequence

static bool test_slideshow_initialization()
{
    printf("Test 1: Slideshow model initialization...\n");

    // Create a 10-item slideshow starting at index 3
    ui::SlideshowModel model(10, 3, ui::SLIDESHOW_DWELL_DEFAULT, false, 0);

    if (model.count() != 10) {
        fprintf(stderr, "FAIL: Expected count 10, got %d\n", model.count());
        return false;
    }

    if (model.index() != 3) {
        fprintf(stderr, "FAIL: Expected start index 3, got %d\n", model.index());
        return false;
    }

    if (!model.running()) {
        fprintf(stderr, "FAIL: Slideshow should start running\n");
        return false;
    }

    if (std::fabs(model.dwell() - ui::SLIDESHOW_DWELL_DEFAULT) > 0.01) {
        fprintf(stderr, "FAIL: Expected dwell %.1f, got %.1f\n", ui::SLIDESHOW_DWELL_DEFAULT, model.dwell());
        return false;
    }

    if (std::fabs(model.fade_progress() - 1.0) > 0.01) {
        fprintf(stderr, "FAIL: Initial fade progress should be 1.0, got %.2f\n", model.fade_progress());
        return false;
    }

    if (model.prev_index() != -1) {
        fprintf(stderr, "FAIL: Initial prev_index should be -1, got %d\n", model.prev_index());
        return false;
    }

    printf("PASS (count, start index, dwell, fade progress correct)\n");
    return true;
}

// Test 2: Advance with wrapping
static bool test_slideshow_advance_wrap()
{
    printf("Test 2: Slideshow advance with wrapping...\n");

    ui::SlideshowModel model(5, 0, ui::SLIDESHOW_DWELL_DEFAULT, false, 0);

    // Advance from 0 to 1
    model.advance(1);
    if (model.index() != 1) {
        fprintf(stderr, "FAIL: Expected index 1 after +1, got %d\n", model.index());
        return false;
    }

    // Advance to end
    model.advance(3);
    if (model.index() != 4) {
        fprintf(stderr, "FAIL: Expected index 4, got %d\n", model.index());
        return false;
    }

    // Advance past end (should wrap)
    model.advance(1);
    if (model.index() != 0) {
        fprintf(stderr, "FAIL: Expected wrap to index 0, got %d\n", model.index());
        return false;
    }

    // Advance backward
    model.advance(-1);
    if (model.index() != 4) {
        fprintf(stderr, "FAIL: Expected backward wrap to index 4, got %d\n", model.index());
        return false;
    }

    printf("PASS (advance and wrap works correctly)\n");
    return true;
}

// Test 3: Dwell clamping
static bool test_slideshow_dwell_clamp()
{
    printf("Test 3: Slideshow dwell clamping...\n");

    ui::SlideshowModel model(10, 0, ui::SLIDESHOW_DWELL_DEFAULT, false, 0);

    // Test adjust up
    model.adjust_dwell(2.0);
    if (std::fabs(model.dwell() - (ui::SLIDESHOW_DWELL_DEFAULT + 2.0)) > 0.01) {
        fprintf(stderr, "FAIL: Expected dwell ~%.1f, got %.1f\n",
                ui::SLIDESHOW_DWELL_DEFAULT + 2.0, model.dwell());
        return false;
    }

    // Test clamp at max
    model.set_dwell(100.0);  // way above max
    if (model.dwell() != ui::SLIDESHOW_DWELL_MAX) {
        fprintf(stderr, "FAIL: Expected dwell clamped to max %.1f, got %.1f\n",
                ui::SLIDESHOW_DWELL_MAX, model.dwell());
        return false;
    }

    // Test clamp at min
    model.set_dwell(0.1);  // below min
    if (model.dwell() != ui::SLIDESHOW_DWELL_MIN) {
        fprintf(stderr, "FAIL: Expected dwell clamped to min %.1f, got %.1f\n",
                ui::SLIDESHOW_DWELL_MIN, model.dwell());
        return false;
    }

    printf("PASS (dwell clamping works)\n");
    return true;
}

// Test 4: Toggle running state
static bool test_slideshow_toggle()
{
    printf("Test 4: Slideshow toggle running state...\n");

    ui::SlideshowModel model(10, 0, ui::SLIDESHOW_DWELL_DEFAULT, false, 0);

    if (!model.running()) {
        fprintf(stderr, "FAIL: Should start running\n");
        return false;
    }

    model.toggle();
    if (model.running()) {
        fprintf(stderr, "FAIL: After toggle, should not be running\n");
        return false;
    }

    model.toggle();
    if (!model.running()) {
        fprintf(stderr, "FAIL: After second toggle, should be running\n");
        return false;
    }

    printf("PASS (toggle works)\n");
    return true;
}

// Test 5: Tick accumulation
static bool test_slideshow_tick()
{
    printf("Test 5: Slideshow tick accumulation...\n");

    ui::SlideshowModel model(10, 0, 1.0, false, 0);  // 1 second dwell

    // Tick 0.5 seconds (should not advance)
    bool changed = model.tick(0.5);
    if (changed || model.index() != 0) {
        fprintf(stderr, "FAIL: 0.5s tick should not advance\n");
        return false;
    }

    // Tick another 0.6 seconds (should cross 1.0s and advance)
    changed = model.tick(0.6);
    if (!changed || model.index() != 1) {
        fprintf(stderr, "FAIL: 1.1s total should advance to index 1, got %d\n", model.index());
        return false;
    }

    printf("PASS (tick accumulation correct)\n");
    return true;
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    int passed = 0;
    int failed = 0;

    if (test_slideshow_initialization()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_slideshow_advance_wrap()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_slideshow_dwell_clamp()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_slideshow_toggle()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_slideshow_tick()) {
        ++passed;
    } else {
        ++failed;
    }

    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
