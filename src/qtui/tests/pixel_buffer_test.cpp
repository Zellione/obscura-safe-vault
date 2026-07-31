#include <cstdio>
#include <cstring>

#include "../pixel_buffer.h"

// Test 1: flatten_alpha_to_black with fully opaque pixel (alpha=255)
static bool test_flatten_opaque_pixel()
{
    printf("Test 1: flatten_alpha_to_black with fully opaque pixel...\n");

    PixelBuffer buf;
    buf.width = 1;
    buf.height = 1;
    buf.rgba = {100, 150, 200, 255};  // RGBA: (100, 150, 200, opaque)

    flatten_alpha_to_black(buf);

    // Opaque pixel should stay the same (R*255/255, G*255/255, B*255/255, 255)
    if (buf.rgba[0] != 100 || buf.rgba[1] != 150 || buf.rgba[2] != 200 || buf.rgba[3] != 255) {
        fprintf(stderr, "FAIL: Expected (100, 150, 200, 255), got (%d, %d, %d, %d)\n",
                buf.rgba[0], buf.rgba[1], buf.rgba[2], buf.rgba[3]);
        return false;
    }

    printf("PASS (opaque pixel unchanged)\n");
    return true;
}

// Test 2: flatten_alpha_to_black with fully transparent pixel (alpha=0)
static bool test_flatten_transparent_pixel()
{
    printf("Test 2: flatten_alpha_to_black with fully transparent pixel...\n");

    PixelBuffer buf;
    buf.width = 1;
    buf.height = 1;
    buf.rgba = {100, 150, 200, 0};  // RGBA: (100, 150, 200, transparent)

    flatten_alpha_to_black(buf);

    // Transparent pixel should become black (0*0/255, 0*0/255, 0*0/255, 255)
    if (buf.rgba[0] != 0 || buf.rgba[1] != 0 || buf.rgba[2] != 0 || buf.rgba[3] != 255) {
        fprintf(stderr, "FAIL: Expected (0, 0, 0, 255), got (%d, %d, %d, %d)\n",
                buf.rgba[0], buf.rgba[1], buf.rgba[2], buf.rgba[3]);
        return false;
    }

    printf("PASS (transparent pixel becomes black)\n");
    return true;
}

// Test 3: flatten_alpha_to_black with semi-transparent pixel (alpha=128)
static bool test_flatten_semi_transparent_pixel()
{
    printf("Test 3: flatten_alpha_to_black with semi-transparent pixel...\n");

    PixelBuffer buf;
    buf.width = 1;
    buf.height = 1;
    buf.rgba = {200, 160, 80, 128};  // RGBA: (200, 160, 80, 50% transparent)

    flatten_alpha_to_black(buf);

    // Semi-transparent pixel: R*128/255 ≈ 100, G*128/255 ≈ 80, B*128/255 ≈ 40
    const int expected_r = (200 * 128) / 255;  // 99 or 100
    const int expected_g = (160 * 128) / 255;  // 79 or 80
    const int expected_b = (80 * 128) / 255;   // 39 or 40

    // Allow ±1 for rounding
    if (buf.rgba[0] < expected_r - 1 || buf.rgba[0] > expected_r + 1 ||
        buf.rgba[1] < expected_g - 1 || buf.rgba[1] > expected_g + 1 ||
        buf.rgba[2] < expected_b - 1 || buf.rgba[2] > expected_b + 1 ||
        buf.rgba[3] != 255) {
        fprintf(stderr, "FAIL: Expected (~%d, ~%d, ~%d, 255), got (%d, %d, %d, %d)\n",
                expected_r, expected_g, expected_b,
                buf.rgba[0], buf.rgba[1], buf.rgba[2], buf.rgba[3]);
        return false;
    }

    printf("PASS (semi-transparent pixel correctly blended)\n");
    return true;
}

// Test 4: flatten_alpha_to_black with multiple pixels
static bool test_flatten_multiple_pixels()
{
    printf("Test 4: flatten_alpha_to_black with multiple pixels...\n");

    PixelBuffer buf;
    buf.width = 2;
    buf.height = 2;
    // 4 pixels: (opaque), (transparent), (semi), (opaque)
    buf.rgba = {
        100, 100, 100, 255,  // pixel 0: opaque red-ish
        200, 200, 200, 0,    // pixel 1: fully transparent
        150, 150, 150, 128,  // pixel 2: semi-transparent
        50, 50, 50, 255      // pixel 3: opaque
    };

    flatten_alpha_to_black(buf);

    // Check pixel 0: should be unchanged
    if (buf.rgba[0] != 100 || buf.rgba[1] != 100 || buf.rgba[2] != 100) {
        fprintf(stderr, "FAIL: pixel 0 should be (100, 100, 100), got (%d, %d, %d)\n",
                buf.rgba[0], buf.rgba[1], buf.rgba[2]);
        return false;
    }

    // Check pixel 1: should be black
    if (buf.rgba[4] != 0 || buf.rgba[5] != 0 || buf.rgba[6] != 0) {
        fprintf(stderr, "FAIL: pixel 1 should be (0, 0, 0), got (%d, %d, %d)\n",
                buf.rgba[4], buf.rgba[5], buf.rgba[6]);
        return false;
    }

    // Check pixel 3: should be unchanged
    if (buf.rgba[12] != 50 || buf.rgba[13] != 50 || buf.rgba[14] != 50) {
        fprintf(stderr, "FAIL: pixel 3 should be (50, 50, 50), got (%d, %d, %d)\n",
                buf.rgba[12], buf.rgba[13], buf.rgba[14]);
        return false;
    }

    // All alpha should now be 255
    for (int i = 3; i < 16; i += 4) {
        if (buf.rgba[i] != 255) {
            fprintf(stderr, "FAIL: alpha at pixel %d should be 255, got %d\n", i/4, buf.rgba[i]);
            return false;
        }
    }

    printf("PASS (multiple pixels correctly processed)\n");
    return true;
}

// Test 5: flatten_alpha_to_black with empty buffer
static bool test_flatten_empty_buffer()
{
    printf("Test 5: flatten_alpha_to_black with empty buffer...\n");

    PixelBuffer buf;
    buf.width = 0;
    buf.height = 0;
    buf.rgba.clear();

    // Should not crash
    flatten_alpha_to_black(buf);

    printf("PASS (empty buffer handled gracefully)\n");
    return true;
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    int passed = 0;
    int failed = 0;

    if (test_flatten_opaque_pixel()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_flatten_transparent_pixel()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_flatten_semi_transparent_pixel()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_flatten_multiple_pixels()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_flatten_empty_buffer()) {
        ++passed;
    } else {
        ++failed;
    }

    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
