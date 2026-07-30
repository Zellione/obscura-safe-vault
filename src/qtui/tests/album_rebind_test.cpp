#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

#include "ui/album_rebind.h"

// Unit tests for ui::album_rebind behavior (path re-resolution after vault refresh)

// Test 1: Current item found in new list → same index, preserve=true
static bool test_album_rebind_item_preserved()
{
    printf("Test 1: Album rebind with item preserved...\n");

    std::vector<std::string> new_paths = {"/favorites/photo1.jpg", "/favorites/photo2.jpg", "/favorites/photo3.jpg"};
    std::string current_path = "/favorites/photo2.jpg";
    int current_index = 1;

    ui::AlbumRebind result = ui::rebind_album_index(new_paths, current_path, current_index);

    if (result.index != 1) {
        fprintf(stderr, "FAIL: Expected index 1, got %d\n", result.index);
        return false;
    }

    if (!result.preserve) {
        fprintf(stderr, "FAIL: Expected preserve=true, got false\n");
        return false;
    }

    printf("PASS (item found, index preserved)\n");
    return true;
}

// Test 2: Current item removed → fallback to same-index item, preserve=false
static bool test_album_rebind_item_removed()
{
    printf("Test 2: Album rebind with item removed...\n");

    std::vector<std::string> new_paths = {"/favorites/photo1.jpg", "/favorites/photo_new.jpg", "/favorites/photo3.jpg"};
    std::string current_path = "/favorites/photo2.jpg";  // Removed
    int current_index = 1;

    ui::AlbumRebind result = ui::rebind_album_index(new_paths, current_path, current_index);

    // Should fallback to index 1 (clamped to size-1 if needed)
    if (result.index < 0 || result.index >= static_cast<int>(new_paths.size())) {
        fprintf(stderr, "FAIL: Index out of bounds: %d (size %zu)\n", result.index, new_paths.size());
        return false;
    }

    if (result.preserve) {
        fprintf(stderr, "FAIL: Expected preserve=false when item removed, got true\n");
        return false;
    }

    printf("PASS (item removed, fallback to same-index)\n");
    return true;
}

// Test 3: Item reordered (moved but present) → new index found, preserve=true
static bool test_album_rebind_item_reordered()
{
    printf("Test 3: Album rebind with item reordered...\n");

    std::vector<std::string> new_paths = {"/favorites/photo_new.jpg", "/favorites/photo2.jpg", "/favorites/photo1.jpg", "/favorites/photo3.jpg"};
    std::string current_path = "/favorites/photo2.jpg";  // Now at index 1, was at index 1
    int current_index = 1;

    ui::AlbumRebind result = ui::rebind_album_index(new_paths, current_path, current_index);

    if (result.index != 1) {
        fprintf(stderr, "FAIL: Expected index 1 (same position), got %d\n", result.index);
        return false;
    }

    if (!result.preserve) {
        fprintf(stderr, "FAIL: Expected preserve=true when item moved, got false\n");
        return false;
    }

    printf("PASS (item reordered, found at new position, state preserved)\n");
    return true;
}

// Test 4: Album emptied (no items left) → fallback to index 0
static bool test_album_rebind_empty_list()
{
    printf("Test 4: Album rebind with empty list fallback...\n");

    std::vector<std::string> new_paths = {};  // Empty
    std::string current_path = "/favorites/photo1.jpg";
    int current_index = 0;

    ui::AlbumRebind result = ui::rebind_album_index(new_paths, current_path, current_index);

    // With empty list, should return index 0
    if (result.index != 0) {
        fprintf(stderr, "FAIL: Expected index 0 for empty list, got %d\n", result.index);
        return false;
    }

    printf("PASS (empty list handled gracefully)\n");
    return true;
}

// Test 5: Multiple items removed → fallback with clamping
static bool test_album_rebind_many_removed()
{
    printf("Test 5: Album rebind with many items removed...\n");

    std::vector<std::string> new_paths = {"/favorites/photo1.jpg", "/favorites/photo2.jpg"};
    std::string current_path = "/favorites/photo5.jpg";  // Was at index 4, but removed
    int current_index = 4;  // Out of range after deletions

    ui::AlbumRebind result = ui::rebind_album_index(new_paths, current_path, current_index);

    // Should clamp to size-1 = 1
    if (result.index != 1) {
        fprintf(stderr, "FAIL: Expected clamped index 1, got %d\n", result.index);
        return false;
    }

    if (result.preserve) {
        fprintf(stderr, "FAIL: Expected preserve=false when item removed, got true\n");
        return false;
    }

    printf("PASS (out-of-range index clamped correctly)\n");
    return true;
}

// Test 6: Item at first position → preserve at first position
static bool test_album_rebind_first_item()
{
    printf("Test 6: Album rebind with first item...\n");

    std::vector<std::string> new_paths = {"/favorites/photo1.jpg", "/favorites/photo2.jpg", "/favorites/photo3.jpg"};
    std::string current_path = "/favorites/photo1.jpg";
    int current_index = 0;

    ui::AlbumRebind result = ui::rebind_album_index(new_paths, current_path, current_index);

    if (result.index != 0) {
        fprintf(stderr, "FAIL: Expected index 0, got %d\n", result.index);
        return false;
    }

    if (!result.preserve) {
        fprintf(stderr, "FAIL: Expected preserve=true\n");
        return false;
    }

    printf("PASS (first item preserved)\n");
    return true;
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    int passed = 0;
    int failed = 0;

    if (test_album_rebind_item_preserved()) ++passed; else ++failed;
    if (test_album_rebind_item_removed()) ++passed; else ++failed;
    if (test_album_rebind_item_reordered()) ++passed; else ++failed;
    if (test_album_rebind_empty_list()) ++passed; else ++failed;
    if (test_album_rebind_many_removed()) ++passed; else ++failed;
    if (test_album_rebind_first_item()) ++passed; else ++failed;

    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
