#include <cstdio>
#include <QCoreApplication>
#include "../playback_engine.h"

// Unit tests for PlaybackEngine video control enhancements (Task 3.5)
// Tests: loop toggle, frame-stepping, volume persistence

static bool test_loop_toggle()
{
    printf("Test 1: Loop toggle initialization and state...\n");

    PlaybackEngine engine;

    // Should start with loop disabled
    if (engine.loopEnabled()) {
        fprintf(stderr, "FAIL: Loop should start disabled\n");
        return false;
    }

    // Toggle to enabled
    engine.toggleLoop();
    if (!engine.loopEnabled()) {
        fprintf(stderr, "FAIL: After toggle, loop should be enabled\n");
        return false;
    }

    // Toggle back to disabled
    engine.toggleLoop();
    if (engine.loopEnabled()) {
        fprintf(stderr, "FAIL: After second toggle, loop should be disabled\n");
        return false;
    }

    printf("PASS (loop toggle works)\n");
    return true;
}

// Test 2: Loop property getter/setter
static bool test_loop_property()
{
    printf("Test 2: Loop property (getter/setter)...\n");

    PlaybackEngine engine;

    // Getter should return false initially
    if (engine.loopEnabled()) {
        fprintf(stderr, "FAIL: Initial loop should be false\n");
        return false;
    }

    // Setter
    engine.setLoopEnabled(true);
    if (!engine.loopEnabled()) {
        fprintf(stderr, "FAIL: After setLoopEnabled(true), should be enabled\n");
        return false;
    }

    // Setter to false
    engine.setLoopEnabled(false);
    if (engine.loopEnabled()) {
        fprintf(stderr, "FAIL: After setLoopEnabled(false), should be disabled\n");
        return false;
    }

    printf("PASS (loop property works)\n");
    return true;
}

// Test 3: Frame-step capability (direction + step size)
static bool test_frame_step()
{
    printf("Test 3: Frame-step (forward/backward)...\n");

    PlaybackEngine engine;

    // stepFrame should succeed even without a video open (just checking the interface)
    // Real stepping requires a decoder with frame timing info

    // Forward step
    engine.stepFrame(1);  // Step forward by 1 frame
    // Can't check result without a real video, but this verifies the interface

    // Backward step
    engine.stepFrame(-1);  // Step backward by 1 frame

    printf("PASS (frame-step interface exists)\n");
    return true;
}

// Test 4: Volume levels are reasonable
static bool test_volume_range()
{
    printf("Test 4: Volume range (0.0-1.0)...\n");

    PlaybackEngine engine;

    // Default volume
    double vol = engine.volume();
    if (vol < 0.0 || vol > 1.0) {
        fprintf(stderr, "FAIL: Default volume %.2f out of range\n", vol);
        return false;
    }

    // Set to min
    engine.setVolume(0.0);
    if (engine.volume() != 0.0) {
        fprintf(stderr, "FAIL: Volume not set to 0.0\n");
        return false;
    }

    // Set to max
    engine.setVolume(1.0);
    if (engine.volume() != 1.0) {
        fprintf(stderr, "FAIL: Volume not set to 1.0\n");
        return false;
    }

    // Out-of-range should be clamped
    engine.setVolume(2.0);
    double clamped = engine.volume();
    if (clamped > 1.0) {
        fprintf(stderr, "FAIL: Volume %.2f should be clamped to <= 1.0\n", clamped);
        return false;
    }

    engine.setVolume(-0.5);
    clamped = engine.volume();
    if (clamped < 0.0) {
        fprintf(stderr, "FAIL: Volume %.2f should be clamped to >= 0.0\n", clamped);
        return false;
    }

    printf("PASS (volume range correct)\n");
    return true;
}

// Test 5: Mute state independent of volume
static bool test_mute_volume_independence()
{
    printf("Test 5: Mute state independent of volume...\n");

    PlaybackEngine engine;

    // Set volume to 0.5
    engine.setVolume(0.5);
    if (engine.volume() != 0.5) {
        fprintf(stderr, "FAIL: Volume should be 0.5\n");
        return false;
    }

    // Toggle mute
    engine.toggleMute();
    if (!engine.muted()) {
        fprintf(stderr, "FAIL: After toggleMute, should be muted\n");
        return false;
    }

    // Volume should remain unchanged
    if (engine.volume() != 0.5) {
        fprintf(stderr, "FAIL: Volume should still be 0.5 while muted\n");
        return false;
    }

    // Unmute
    engine.toggleMute();
    if (engine.muted()) {
        fprintf(stderr, "FAIL: After toggleMute again, should not be muted\n");
        return false;
    }

    printf("PASS (mute independent of volume)\n");
    return true;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    int passed = 0;
    int failed = 0;

    if (test_loop_toggle()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_loop_property()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_frame_step()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_volume_range()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_mute_volume_independence()) {
        ++passed;
    } else {
        ++failed;
    }

    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
