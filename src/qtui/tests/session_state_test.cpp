#include <cstdio>

#include <QGuiApplication>
#include <QtTest/QSignalSpy>

#include "session_state.h"

// Test 1: Per-path focus index round-trip
static bool test_per_path_focus()
{
    printf("Test 1: Per-path focus index round-trip...\n");

    SessionState state;

    // Set focus at path "a/b"
    state.recordFocusIndex("a/b", 5);
    int recalled = state.recallFocusIndex("a/b");
    if (recalled != 5) {
        fprintf(stderr, "FAIL: Focus at 'a/b' should be 5, got %d\n", recalled);
        return false;
    }

    // Different path has default (0)
    int other = state.recallFocusIndex("c/d");
    if (other != 0) {
        fprintf(stderr, "FAIL: Focus at 'c/d' should default to 0, got %d\n", other);
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 2: Multiple path focus independence
static bool test_multiple_paths()
{
    printf("Test 2: Multiple paths independent...\n");

    SessionState state;

    state.recordFocusIndex("path1", 1);
    state.recordFocusIndex("path2", 2);
    state.recordFocusIndex("path3", 3);

    if (state.recallFocusIndex("path1") != 1 ||
        state.recallFocusIndex("path2") != 2 ||
        state.recallFocusIndex("path3") != 3) {
        fprintf(stderr, "FAIL: Path focus values don't match\n");
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 3: View density getter/setter
static bool test_view_density()
{
    printf("Test 3: View density persistence...\n");

    SessionState state;

    // Default
    int defaultDensity = state.viewDensity();
    // We don't know the default, but it should be consistent
    if (defaultDensity < 0) {
        fprintf(stderr, "FAIL: View density should be non-negative\n");
        return false;
    }

    // Set and retrieve
    state.setViewDensity(3);
    if (state.viewDensity() != 3) {
        fprintf(stderr, "FAIL: View density should be 3, got %d\n", state.viewDensity());
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 4: Detail open toggle
static bool test_detail_open()
{
    printf("Test 4: Detail panel state...\n");

    SessionState state;

    // Default should be closed
    if (state.detailOpen()) {
        fprintf(stderr, "FAIL: Detail should default to closed (false)\n");
        return false;
    }

    state.setDetailOpen(true);
    if (!state.detailOpen()) {
        fprintf(stderr, "FAIL: Detail should be open after setDetailOpen(true)\n");
        return false;
    }

    state.setDetailOpen(false);
    if (state.detailOpen()) {
        fprintf(stderr, "FAIL: Detail should be closed after setDetailOpen(false)\n");
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 5: Strip side getter/setter
static bool test_strip_side()
{
    printf("Test 5: Strip side persistence...\n");

    SessionState state;

    // Default (we don't know what, but should be one of the expected values)
    int defaultSide = state.stripSide();
    if (defaultSide < 0 || defaultSide > 3) {
        fprintf(stderr, "FAIL: Strip side should be 0-3, got %d\n", defaultSide);
        return false;
    }

    // Set and retrieve
    state.setStripSide(1);
    if (state.stripSide() != 1) {
        fprintf(stderr, "FAIL: Strip side should be 1, got %d\n", state.stripSide());
        return false;
    }

    state.setStripSide(2);
    if (state.stripSide() != 2) {
        fprintf(stderr, "FAIL: Strip side should be 2, got %d\n", state.stripSide());
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 6: Video resume bookmark
static bool test_video_resume()
{
    printf("Test 6: Video resume bookmark...\n");

    SessionState state;

    // Default should be empty/zero
    if (!state.lastMediaPath().isEmpty()) {
        fprintf(stderr, "FAIL: lastMediaPath should default to empty\n");
        return false;
    }
    if (state.videoResumeSeconds() != 0.0) {
        fprintf(stderr, "FAIL: videoResumeSeconds should default to 0.0\n");
        return false;
    }

    // Set and retrieve
    state.setLastMediaPath("gallery/video1.mp4");
    state.setVideoResumeSeconds(123.456);

    if (state.lastMediaPath() != "gallery/video1.mp4") {
        fprintf(stderr, "FAIL: lastMediaPath mismatch\n");
        return false;
    }
    if (state.videoResumeSeconds() != 123.456) {
        fprintf(stderr, "FAIL: videoResumeSeconds mismatch\n");
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 7: Video resume overwrite
static bool test_video_resume_overwrite()
{
    printf("Test 7: Video resume overwrites on new media...\n");

    SessionState state;

    state.setLastMediaPath("video1.mp4");
    state.setVideoResumeSeconds(100.0);

    // New video overwrites old
    state.setLastMediaPath("video2.mp4");
    state.setVideoResumeSeconds(50.0);

    if (state.lastMediaPath() != "video2.mp4" || state.videoResumeSeconds() != 50.0) {
        fprintf(stderr, "FAIL: Video resume should be updated\n");
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 8: Reset clears everything
static bool test_reset()
{
    printf("Test 8: reset() clears all state...\n");

    SessionState state;

    state.recordFocusIndex("a/b", 5);
    state.setViewDensity(2);
    state.setDetailOpen(true);
    state.setStripSide(1);
    state.setLastMediaPath("video.mp4");
    state.setVideoResumeSeconds(42.0);

    state.reset();

    if (state.recallFocusIndex("a/b") != 0) {
        fprintf(stderr, "FAIL: Focus should reset to 0\n");
        return false;
    }
    if (state.lastMediaPath() != "") {
        fprintf(stderr, "FAIL: lastMediaPath should reset to empty\n");
        return false;
    }
    if (state.videoResumeSeconds() != 0.0) {
        fprintf(stderr, "FAIL: videoResumeSeconds should reset to 0.0\n");
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 9: setViewDensity no-op doesn't emit signal
static bool test_view_density_no_op()
{
    printf("Test 9: setViewDensity no-op skips signal...\n");

    QGuiApplication::processEvents();

    SessionState state;
    QSignalSpy spy(&state, &SessionState::viewDensityChanged);

    // Set to value 1
    state.setViewDensity(1);
    if (spy.count() != 1) {
        fprintf(stderr, "FAIL: First setViewDensity should emit, got %d signals\n", spy.count());
        return false;
    }

    // Set to same value again (no-op)
    spy.clear();
    state.setViewDensity(1);
    if (spy.count() != 0) {
        fprintf(stderr, "FAIL: No-op setViewDensity should not emit, got %d signals\n", spy.count());
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 10: setStripSide no-op doesn't emit signal
static bool test_strip_side_no_op()
{
    printf("Test 10: setStripSide no-op skips signal...\n");

    QGuiApplication::processEvents();

    SessionState state;
    QSignalSpy spy(&state, &SessionState::stripSideChanged);

    // Set to value 2
    state.setStripSide(2);
    if (spy.count() != 1) {
        fprintf(stderr, "FAIL: First setStripSide should emit, got %d signals\n", spy.count());
        return false;
    }

    // Set to same value again (no-op)
    spy.clear();
    state.setStripSide(2);
    if (spy.count() != 0) {
        fprintf(stderr, "FAIL: No-op setStripSide should not emit, got %d signals\n", spy.count());
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 11: Custom data round-trip
static bool test_custom_data()
{
    printf("Test 11: Custom data round-trip...\n");

    SessionState state;

    // Store and retrieve JSON data
    const QString jsonData = R"({"include":"tag1","exclude":"tag2","name":"test"})";
    state.recordCustomData("adv_search_query", jsonData);

    const QString retrieved = state.recallCustomData("adv_search_query");
    if (retrieved != jsonData) {
        fprintf(stderr, "FAIL: Retrieved data doesn't match stored data\n");
        fprintf(stderr, "  Expected: %s\n", jsonData.toStdString().c_str());
        fprintf(stderr, "  Got: %s\n", retrieved.toStdString().c_str());
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 12: Custom data with multiple keys
static bool test_custom_data_multiple_keys()
{
    printf("Test 12: Multiple custom data keys independent...\n");

    SessionState state;

    state.recordCustomData("key1", "value1");
    state.recordCustomData("key2", "value2");
    state.recordCustomData("key3", "value3");

    if (state.recallCustomData("key1") != "value1" ||
        state.recallCustomData("key2") != "value2" ||
        state.recallCustomData("key3") != "value3") {
        fprintf(stderr, "FAIL: Custom data values don't match\n");
        return false;
    }

    // Unknown key should return empty string
    if (!state.recallCustomData("unknown").isEmpty()) {
        fprintf(stderr, "FAIL: Unknown key should return empty string\n");
        return false;
    }

    printf("PASS\n");
    return true;
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    qRegisterMetaType<QString>("QString");

    int passed = 0;
    int failed = 0;

    if (test_per_path_focus()) { ++passed; } else { ++failed; }
    if (test_multiple_paths()) { ++passed; } else { ++failed; }
    if (test_view_density()) { ++passed; } else { ++failed; }
    if (test_detail_open()) { ++passed; } else { ++failed; }
    if (test_strip_side()) { ++passed; } else { ++failed; }
    if (test_video_resume()) { ++passed; } else { ++failed; }
    if (test_video_resume_overwrite()) { ++passed; } else { ++failed; }
    if (test_reset()) { ++passed; } else { ++failed; }
    if (test_view_density_no_op()) { ++passed; } else { ++failed; }
    if (test_strip_side_no_op()) { ++passed; } else { ++failed; }
    if (test_custom_data()) { ++passed; } else { ++failed; }
    if (test_custom_data_multiple_keys()) { ++passed; } else { ++failed; }

    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
