#include <cstdio>
#include <cstdlib>
#include <vector>

#include <QGuiApplication>
#include <QtTest/QSignalSpy>

#include "selection_controller.h"

// Mock row→name lookup for testing
static std::vector<QString> test_names = {"image1", "gallery1", "image2", "image3"};

class MockGalleryModel {
public:
    QString nameAt(int row) const
    {
        if (row >= 0 && row < static_cast<int>(test_names.size())) {
            return test_names[row];
        }
        return QString();
    }
};

// Test 1: toggle adds and removes selections
static bool test_toggle_basic()
{
    printf("Test 1: toggle adds/removes selections...\n");

    MockGalleryModel model;
    SelectionController controller;
    controller.setNameLookup([&model](int row) { return model.nameAt(row); });

    // Initially empty
    if (controller.count() != 0) {
        fprintf(stderr, "FAIL: Initial count should be 0, got %d\n", controller.count());
        return false;
    }

    // Toggle row 0 (add)
    controller.toggle(0);
    if (controller.count() != 1 || !controller.isSelected(0)) {
        fprintf(stderr, "FAIL: After toggle(0), count=%d, isSelected(0)=%d\n",
                controller.count(), controller.isSelected(0));
        return false;
    }

    // Toggle row 0 again (remove)
    controller.toggle(0);
    if (controller.count() != 0 || controller.isSelected(0)) {
        fprintf(stderr, "FAIL: After second toggle(0), count=%d, isSelected(0)=%d\n",
                controller.count(), controller.isSelected(0));
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 2: isSelected returns correct state
static bool test_is_selected()
{
    printf("Test 2: isSelected reflects state...\n");

    MockGalleryModel model;
    SelectionController controller;
    controller.setNameLookup([&model](int row) { return model.nameAt(row); });

    controller.toggle(1);
    controller.toggle(3);

    if (!controller.isSelected(1)) {
        fprintf(stderr, "FAIL: Row 1 should be selected\n");
        return false;
    }
    if (!controller.isSelected(3)) {
        fprintf(stderr, "FAIL: Row 3 should be selected\n");
        return false;
    }
    if (controller.isSelected(0) || controller.isSelected(2)) {
        fprintf(stderr, "FAIL: Rows 0,2 should not be selected\n");
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 3: count property works
static bool test_count_property()
{
    printf("Test 3: count property...\n");

    MockGalleryModel model;
    SelectionController controller;
    controller.setNameLookup([&model](int row) { return model.nameAt(row); });

    if (controller.count() != 0) {
        fprintf(stderr, "FAIL: Initial count should be 0\n");
        return false;
    }

    controller.toggle(0);
    controller.toggle(2);
    if (controller.count() != 2) {
        fprintf(stderr, "FAIL: After 2 toggles, count should be 2, got %d\n", controller.count());
        return false;
    }

    controller.toggle(2);  // Remove
    if (controller.count() != 1) {
        fprintf(stderr, "FAIL: After remove, count should be 1, got %d\n", controller.count());
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 4: clear removes all selections
static bool test_clear()
{
    printf("Test 4: clear empties selection...\n");

    MockGalleryModel model;
    SelectionController controller;
    controller.setNameLookup([&model](int row) { return model.nameAt(row); });

    controller.toggle(0);
    controller.toggle(1);
    controller.toggle(3);

    if (controller.count() != 3) {
        fprintf(stderr, "FAIL: Before clear, count should be 3\n");
        return false;
    }

    controller.clear();

    if (controller.count() != 0) {
        fprintf(stderr, "FAIL: After clear, count should be 0\n");
        return false;
    }
    if (controller.isSelected(0) || controller.isSelected(1) || controller.isSelected(3)) {
        fprintf(stderr, "FAIL: After clear, nothing should be selected\n");
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 5: Phase 53 Ctrl+A — empty gallery inert (select_all(0) doesn't change)
static bool test_ctrl_a_empty_gallery_inert()
{
    printf("Test 5: Ctrl+A on empty gallery is inert...\n");

    MockGalleryModel model;
    SelectionController controller;
    controller.setNameLookup([&model](int row) { return model.nameAt(row); });

    // Set count to 0 (empty gallery simulation)
    int empty_count = 0;

    // toggleAll() on empty — should stay empty
    controller.toggleAll(empty_count);
    if (controller.count() != 0) {
        fprintf(stderr, "FAIL: toggleAll(0) should leave selection empty, got count=%d\n",
                controller.count());
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 6: Phase 53 Ctrl+A — select all when empty→1 item
static bool test_ctrl_a_select_all_single()
{
    printf("Test 6: Ctrl+A selects all (single item)...\n");

    MockGalleryModel model;
    SelectionController controller;
    controller.setNameLookup([&model](int row) { return model.nameAt(row); });

    // toggleAll on count=1: should select the one item
    controller.toggleAll(1);
    if (controller.count() != 1 || !controller.isSelected(0)) {
        fprintf(stderr, "FAIL: toggleAll(1) should select row 0\n");
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 7: Phase 53 Ctrl+A — select all when all are already selected
static bool test_ctrl_a_select_all_multi()
{
    printf("Test 7: Ctrl+A selects all (multiple items)...\n");

    MockGalleryModel model;
    SelectionController controller;
    controller.setNameLookup([&model](int row) { return model.nameAt(row); });

    // toggleAll on count=4: should select all
    controller.toggleAll(4);
    if (controller.count() != 4) {
        fprintf(stderr, "FAIL: toggleAll(4) should select 4 items, got %d\n", controller.count());
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (!controller.isSelected(i)) {
            fprintf(stderr, "FAIL: Row %d should be selected after toggleAll\n", i);
            return false;
        }
    }

    printf("PASS\n");
    return true;
}

// Test 8: Phase 53 Ctrl+A — toggle from all-selected to empty
static bool test_ctrl_a_toggle_clear()
{
    printf("Test 8: Ctrl+A toggle clears when all selected...\n");

    MockGalleryModel model;
    SelectionController controller;
    controller.setNameLookup([&model](int row) { return model.nameAt(row); });

    // Select all 4
    controller.toggleAll(4);
    if (controller.count() != 4) {
        fprintf(stderr, "FAIL: toggleAll(4) should select all\n");
        return false;
    }

    // toggleAll again (all selected → clear)
    controller.toggleAll(4);
    if (controller.count() != 0) {
        fprintf(stderr, "FAIL: Second toggleAll(4) should clear all, got count=%d\n",
                controller.count());
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 9: Phase 53 Ctrl+A — partial selection → select all
static bool test_ctrl_a_partial_to_all()
{
    printf("Test 9: Ctrl+A selects remaining when partial...\n");

    MockGalleryModel model;
    SelectionController controller;
    controller.setNameLookup([&model](int row) { return model.nameAt(row); });

    // Select 2 of 4
    controller.toggle(0);
    controller.toggle(2);
    if (controller.count() != 2) {
        fprintf(stderr, "FAIL: Should have 2 selected\n");
        return false;
    }

    // toggleAll on count=4 with partial selection
    controller.toggleAll(4);
    if (controller.count() != 4) {
        fprintf(stderr, "FAIL: toggleAll should select all, got %d\n", controller.count());
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 10: rangeSelectTo adds range from last to row
static bool test_range_select_to()
{
    printf("Test 10: rangeSelectTo extends selection...\n");

    MockGalleryModel model;
    SelectionController controller;
    controller.setNameLookup([&model](int row) { return model.nameAt(row); });

    // Start with row 1
    controller.toggle(1);
    if (!controller.isSelected(1)) {
        fprintf(stderr, "FAIL: Initial toggle failed\n");
        return false;
    }

    // Range select to row 3 (should select 1, 2, 3)
    controller.rangeSelectTo(3);
    if (controller.count() != 3) {
        fprintf(stderr, "FAIL: rangeSelectTo should give 3 selected, got %d\n", controller.count());
        return false;
    }
    if (!controller.isSelected(1) || !controller.isSelected(2) || !controller.isSelected(3)) {
        fprintf(stderr, "FAIL: Range 1-3 not all selected\n");
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 11: Name-keyed selection survives row refetch
static bool test_name_keyed_survival()
{
    printf("Test 11: Selection survives refetch by name...\n");

    MockGalleryModel model;
    SelectionController controller;
    controller.setNameLookup([&model](int row) { return model.nameAt(row); });

    // Select items by row index (which map to names)
    controller.toggle(0);  // "image1"
    controller.toggle(2);  // "image2"
    if (controller.count() != 2) {
        fprintf(stderr, "FAIL: Initial selection failed\n");
        return false;
    }

    // Simulate a model refetch: rows reordered
    // Original: ["image1", "gallery1", "image2", "image3"]
    // Refetch:  ["gallery1", "image1", "image2", "image3"]
    std::vector<QString> old_names = test_names;
    test_names = {"gallery1", "image1", "image2", "image3"};

    // Recreate controller and restore by name
    SelectionController controller2;
    controller2.setNameLookup([&model](int row) { return model.nameAt(row); });

    // Manually restore selection by name (this is what the adapter should do)
    QList<QString> selected_names = controller.selectedNames();
    for (int row = 0; row < static_cast<int>(test_names.size()); ++row) {
        if (selected_names.contains(model.nameAt(row))) {
            controller2.toggle(row);
        }
    }

    // Restore original names for cleanup
    test_names = old_names;

    if (controller2.count() != 2) {
        fprintf(stderr, "FAIL: After refetch, selection count should be 2, got %d\n",
                controller2.count());
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 12: selectedNames returns list of selected item names
static bool test_selected_names()
{
    printf("Test 12: selectedNames returns correct names...\n");

    MockGalleryModel model;
    SelectionController controller;
    controller.setNameLookup([&model](int row) { return model.nameAt(row); });

    controller.toggle(0);  // "image1"
    controller.toggle(2);  // "image2"

    QList<QString> names = controller.selectedNames();
    if (names.size() != 2) {
        fprintf(stderr, "FAIL: Expected 2 names, got %d\n", names.size());
        return false;
    }
    if (!names.contains("image1") || !names.contains("image2")) {
        fprintf(stderr, "FAIL: Expected ['image1', 'image2'], got [%s, %s]\n",
                names[0].toStdString().c_str(), names[1].toStdString().c_str());
        return false;
    }

    printf("PASS\n");
    return true;
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    int passed = 0;
    int failed = 0;

    if (test_toggle_basic()) { ++passed; } else { ++failed; }
    if (test_is_selected()) { ++passed; } else { ++failed; }
    if (test_count_property()) { ++passed; } else { ++failed; }
    if (test_clear()) { ++passed; } else { ++failed; }
    if (test_ctrl_a_empty_gallery_inert()) { ++passed; } else { ++failed; }
    if (test_ctrl_a_select_all_single()) { ++passed; } else { ++failed; }
    if (test_ctrl_a_select_all_multi()) { ++passed; } else { ++failed; }
    if (test_ctrl_a_toggle_clear()) { ++passed; } else { ++failed; }
    if (test_ctrl_a_partial_to_all()) { ++passed; } else { ++failed; }
    if (test_range_select_to()) { ++passed; } else { ++failed; }
    if (test_name_keyed_survival()) { ++passed; } else { ++failed; }
    if (test_selected_names()) { ++passed; } else { ++failed; }

    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
