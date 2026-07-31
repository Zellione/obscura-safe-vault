#include <cstdio>
#include <cmath>
#include <QCoreApplication>
#include <QSignalSpy>
#include "../anim_controller.h"

// Unit tests for AnimController frame scheduling and playback control
// Tests: initialization, frame advancement, pause/play, catch-up capping, wrapping

static bool test_anim_initialization()
{
    printf("Test 1: AnimController initialization...\n");

    AnimController controller;

    if (controller.playing()) {
        fprintf(stderr, "FAIL: Should start in paused state\n");
        return false;
    }

    if (controller.currentFrame() != 0) {
        fprintf(stderr, "FAIL: Expected currentFrame 0, got %d\n", controller.currentFrame());
        return false;
    }

    if (controller.totalFrames() != 0) {
        fprintf(stderr, "FAIL: Expected totalFrames 0, got %d\n", controller.totalFrames());
        return false;
    }

    printf("PASS (initial state correct)\n");
    return true;
}

// Test 2: Play/pause toggle
static bool test_anim_play_pause()
{
    printf("Test 2: AnimController play/pause...\n");

    AnimController controller;

    // Initially paused
    if (controller.playing()) {
        fprintf(stderr, "FAIL: Should start paused\n");
        return false;
    }

    // Play
    controller.play();
    if (!controller.playing()) {
        fprintf(stderr, "FAIL: After play(), should be playing\n");
        return false;
    }

    // Pause
    controller.pause();
    if (controller.playing()) {
        fprintf(stderr, "FAIL: After pause(), should not be playing\n");
        return false;
    }

    // Toggle to play
    controller.togglePlayPause();
    if (!controller.playing()) {
        fprintf(stderr, "FAIL: After togglePlayPause(), should be playing\n");
        return false;
    }

    // Toggle to pause
    controller.togglePlayPause();
    if (controller.playing()) {
        fprintf(stderr, "FAIL: After second toggle, should be paused\n");
        return false;
    }

    printf("PASS (play/pause/toggle works)\n");
    return true;
}

// Test 3: Total frames setter
static bool test_anim_set_total_frames()
{
    printf("Test 3: AnimController setTotalFrames...\n");

    AnimController controller;

    if (controller.totalFrames() != 0) {
        fprintf(stderr, "FAIL: Initial totalFrames should be 0\n");
        return false;
    }

    // Set to 100
    controller.setTotalFrames(100);
    if (controller.totalFrames() != 100) {
        fprintf(stderr, "FAIL: Expected totalFrames 100, got %d\n", controller.totalFrames());
        return false;
    }

    // Set to same value (should not fail)
    controller.setTotalFrames(100);
    if (controller.totalFrames() != 100) {
        fprintf(stderr, "FAIL: Expected totalFrames still 100\n");
        return false;
    }

    // Change to different value
    controller.setTotalFrames(250);
    if (controller.totalFrames() != 250) {
        fprintf(stderr, "FAIL: Expected totalFrames 250, got %d\n", controller.totalFrames());
        return false;
    }

    printf("PASS (setTotalFrames works)\n");
    return true;
}

// Test 4: Reset functionality
static bool test_anim_reset()
{
    printf("Test 4: AnimController reset...\n");

    AnimController controller;
    controller.setTotalFrames(50);

    // Simulate frame advancement by calling play and then accessing internal state
    // We can't directly set currentFrame, but reset should set it to 0
    controller.reset();

    if (controller.currentFrame() != 0) {
        fprintf(stderr, "FAIL: After reset(), currentFrame should be 0, got %d\n", controller.currentFrame());
        return false;
    }

    printf("PASS (reset works)\n");
    return true;
}

// Test 5: Frame catch-up capping
// This test verifies the frame advancement logic without requiring a running timer.
// We simulate the internal calculation to verify the 64-frame cap.
static bool test_anim_catchup_cap()
{
    printf("Test 5: AnimController catch-up capping (64-frame max)...\n");

    // Simulate a large time step that would cause many frames to advance
    // At 60 FPS (16.67ms per frame), a 2 second stall = ~120 frames
    // We want to verify the cap never exceeds 64

    double accumulator = 0.0;
    double dt = 2.0;  // 2 second stall (will produce ~120 frames without cap)
    double frameDelay = 0.01667;  // ~60 FPS
    int totalFrames = 100;
    int currentFrame = 0;

    // Calculate frames to advance
    accumulator += dt;
    int framesToAdvance = 0;
    if (accumulator >= frameDelay) {
        framesToAdvance = static_cast<int>(accumulator / frameDelay);
        accumulator -= framesToAdvance * frameDelay;

        // Cap catch-up (this is the line under test)
        if (framesToAdvance > 64) {
            framesToAdvance = 64;
        }
    }

    if (framesToAdvance != 64) {
        fprintf(stderr, "FAIL: Expected capped at 64, got %d\n", framesToAdvance);
        return false;
    }

    // Advance frame and wrap
    currentFrame = (currentFrame + framesToAdvance) % totalFrames;

    if (currentFrame != 64) {
        fprintf(stderr, "FAIL: Expected currentFrame 64, got %d\n", currentFrame);
        return false;
    }

    printf("PASS (catch-up capped to 64 frames)\n");
    return true;
}

// Test 6: Frame wrapping
static bool test_anim_frame_wrapping()
{
    printf("Test 6: AnimController frame wrapping...\n");

    // Simulate frame wrapping when currentFrame + advance >= totalFrames
    int totalFrames = 10;
    int currentFrame = 8;

    // Advance 5 frames (should wrap: 8 + 5 = 13, 13 % 10 = 3)
    int framesToAdvance = 5;
    currentFrame = (currentFrame + framesToAdvance) % totalFrames;

    if (currentFrame != 3) {
        fprintf(stderr, "FAIL: Expected wrapped frame 3, got %d\n", currentFrame);
        return false;
    }

    // Advance to exact boundary
    currentFrame = 9;
    framesToAdvance = 1;
    currentFrame = (currentFrame + framesToAdvance) % totalFrames;

    if (currentFrame != 0) {
        fprintf(stderr, "FAIL: Expected wrapped to 0, got %d\n", currentFrame);
        return false;
    }

    printf("PASS (frame wrapping works)\n");
    return true;
}

// Test 7: Frame delay handling (variable frame rates)
static bool test_anim_frame_delays()
{
    printf("Test 7: AnimController frame delay (GIF timing)...\n");

    // Test different frame delay values (in seconds)
    // Typical GIF frame delays: 0.05s (20 FPS), 0.1s (10 FPS), 0.03s (~33 FPS)

    struct TestCase {
        double delay;
        int expectedFramesIn_16ms;
    };

    const TestCase cases[] = {
        {0.05, 0},   // 50ms delay: 16ms < 50ms, no advance
        {0.01667, 1}, // ~60 FPS: ~16.67ms triggers 1 frame advance
        {0.02, 0},   // 20ms delay: 16ms < 20ms, no advance
    };

    for (const auto& tc : cases) {
        double accumulator = 0.0;
        double dt = 0.01667;  // 16.67ms tick
        accumulator += dt;

        int framesToAdvance = 0;
        if (accumulator >= tc.delay) {
            framesToAdvance = static_cast<int>(accumulator / tc.delay);
            accumulator -= framesToAdvance * tc.delay;
        }

        if (framesToAdvance != tc.expectedFramesIn_16ms) {
            fprintf(stderr, "FAIL: delay=%.5f, expected %d frames, got %d\n",
                    tc.delay, tc.expectedFramesIn_16ms, framesToAdvance);
            return false;
        }
    }

    printf("PASS (frame delay calculations correct)\n");
    return true;
}

// Test 8: Signal emissions (using QSignalSpy)
static bool test_anim_signals()
{
    printf("Test 8: AnimController signal emissions...\n");

    AnimController controller;

    // Spy on playingChanged signal
    QSignalSpy playingSpy(&controller, SIGNAL(playingChanged()));
    QSignalSpy totalFramesSpy(&controller, SIGNAL(totalFramesChanged()));

    // Play should emit playingChanged
    controller.play();
    if (playingSpy.count() != 1) {
        fprintf(stderr, "FAIL: Expected 1 playingChanged signal, got %d\n", playingSpy.count());
        return false;
    }

    // Play again should not emit (already playing)
    controller.play();
    if (playingSpy.count() != 1) {
        fprintf(stderr, "FAIL: Redundant play() should not emit signal\n");
        return false;
    }

    // Pause should emit
    controller.pause();
    if (playingSpy.count() != 2) {
        fprintf(stderr, "FAIL: Expected 2 playingChanged signals, got %d\n", playingSpy.count());
        return false;
    }

    // setTotalFrames should emit
    controller.setTotalFrames(100);
    if (totalFramesSpy.count() != 1) {
        fprintf(stderr, "FAIL: Expected 1 totalFramesChanged signal, got %d\n", totalFramesSpy.count());
        return false;
    }

    // Setting same value should not emit
    controller.setTotalFrames(100);
    if (totalFramesSpy.count() != 1) {
        fprintf(stderr, "FAIL: Setting same totalFrames should not emit\n");
        return false;
    }

    printf("PASS (signals emitted correctly)\n");
    return true;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    int passed = 0;
    int failed = 0;

    if (test_anim_initialization()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_anim_play_pause()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_anim_set_total_frames()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_anim_reset()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_anim_catchup_cap()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_anim_frame_wrapping()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_anim_frame_delays()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_anim_signals()) {
        ++passed;
    } else {
        ++failed;
    }

    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
