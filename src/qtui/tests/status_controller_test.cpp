#include <cstdio>
#include <QGuiApplication>
#include <QtTest/QSignalSpy>

#include "status_controller.h"

// Test 1: set(kind, text) stores and resolves to the new text
static bool test_set_normal_status()
{
    printf("Test 1: set(NORMAL, text) stores and resolves...\n");

    auto controller = std::make_unique<StatusController>();

    controller->set(StatusController::Normal, "Status message");

    if (controller->text() != "Status message") {
        fprintf(stderr, "FAIL: text is '%s', expected 'Status message'\n",
                controller->text().toStdString().c_str());
        return false;
    }

    if (controller->kind() != StatusController::Normal) {
        fprintf(stderr, "FAIL: kind is %d, expected %d\n",
                controller->kind(), StatusController::Normal);
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 2: set(IMPORT, text) has higher priority than NORMAL
static bool test_import_priority_over_normal()
{
    printf("Test 2: set(IMPORT) priority over NORMAL...\n");

    auto controller = std::make_unique<StatusController>();

    controller->set(StatusController::Normal, "Status");
    controller->set(StatusController::Import, "Importing");

    if (controller->text() != "Importing") {
        fprintf(stderr, "FAIL: text is '%s', expected 'Importing'\n",
                controller->text().toStdString().c_str());
        return false;
    }

    if (controller->kind() != StatusController::Import) {
        fprintf(stderr, "FAIL: kind is %d, expected %d\n",
                controller->kind(), StatusController::Import);
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 3: set(ERROR, text) has highest priority
static bool test_error_priority_over_all()
{
    printf("Test 3: set(ERROR) priority over IMPORT and NORMAL...\n");

    auto controller = std::make_unique<StatusController>();

    controller->set(StatusController::Normal, "Status");
    controller->set(StatusController::Import, "Importing");
    controller->set(StatusController::Error, "Error!");

    if (controller->text() != "Error!") {
        fprintf(stderr, "FAIL: text is '%s', expected 'Error!'\n",
                controller->text().toStdString().c_str());
        return false;
    }

    if (controller->kind() != StatusController::Error) {
        fprintf(stderr, "FAIL: kind is %d, expected %d\n",
                controller->kind(), StatusController::Error);
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 4: clearKind(ERROR) reveals next priority (IMPORT)
static bool test_clear_error_reveals_import()
{
    printf("Test 4: clearKind(ERROR) reveals IMPORT...\n");

    auto controller = std::make_unique<StatusController>();

    controller->set(StatusController::Normal, "Status");
    controller->set(StatusController::Import, "Importing");
    controller->set(StatusController::Error, "Error!");

    // Clear error; should show import
    controller->clearKind(StatusController::Error);

    if (controller->text() != "Importing") {
        fprintf(stderr, "FAIL: text is '%s', expected 'Importing'\n",
                controller->text().toStdString().c_str());
        return false;
    }

    if (controller->kind() != StatusController::Import) {
        fprintf(stderr, "FAIL: kind is %d, expected %d\n",
                controller->kind(), StatusController::Import);
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 5: clearKind(IMPORT) reveals NORMAL
static bool test_clear_import_reveals_normal()
{
    printf("Test 5: clearKind(IMPORT) reveals NORMAL...\n");

    auto controller = std::make_unique<StatusController>();

    controller->set(StatusController::Normal, "Status");
    controller->set(StatusController::Import, "Importing");

    controller->clearKind(StatusController::Import);

    if (controller->text() != "Status") {
        fprintf(stderr, "FAIL: text is '%s', expected 'Status'\n",
                controller->text().toStdString().c_str());
        return false;
    }

    if (controller->kind() != StatusController::Normal) {
        fprintf(stderr, "FAIL: kind is %d, expected %d\n",
                controller->kind(), StatusController::Normal);
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 6: clearKind on empty kind is no-op
static bool test_clear_empty_kind_noop()
{
    printf("Test 6: clearKind on empty kind is no-op...\n");

    auto controller = std::make_unique<StatusController>();

    controller->set(StatusController::Normal, "Status");

    // Try to clear import (which is empty)
    controller->clearKind(StatusController::Import);

    if (controller->text() != "Status") {
        fprintf(stderr, "FAIL: text is '%s', expected 'Status'\n",
                controller->text().toStdString().c_str());
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 7: set with empty text clears that kind
static bool test_set_empty_text_clears()
{
    printf("Test 7: set with empty text clears kind...\n");

    auto controller = std::make_unique<StatusController>();

    controller->set(StatusController::Normal, "Status");
    controller->set(StatusController::Error, "Error!");

    // Set error to empty (clears it)
    controller->set(StatusController::Error, "");

    if (controller->text() != "Status") {
        fprintf(stderr, "FAIL: text is '%s', expected 'Status'\n",
                controller->text().toStdString().c_str());
        return false;
    }

    if (controller->kind() != StatusController::Normal) {
        fprintf(stderr, "FAIL: kind is %d, expected %d\n",
                controller->kind(), StatusController::Normal);
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 8: text signal emitted on change
static bool test_text_changed_signal()
{
    printf("Test 8: text changed signal emitted...\n");

    QGuiApplication::processEvents();

    auto controller = std::make_unique<StatusController>();
    QSignalSpy textChangedSpy(controller.get(), &StatusController::textChanged);

    controller->set(StatusController::Normal, "Status");

    if (textChangedSpy.count() != 1) {
        fprintf(stderr, "FAIL: textChanged signal emitted %d times, expected 1\n",
                textChangedSpy.count());
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 9: kind signal emitted on priority change (Import)
static bool test_kind_changed_signal()
{
    printf("Test 9: kind changed signal emitted on kind change...\n");

    QGuiApplication::processEvents();

    auto controller = std::make_unique<StatusController>();
    QSignalSpy kindChangedSpy(controller.get(), &StatusController::kindChanged);

    // Start with Normal
    controller->set(StatusController::Normal, "Status");
    kindChangedSpy.clear();  // Clear the spy to ignore the initial change (if any)

    // Change to Import (this should emit kindChanged)
    controller->set(StatusController::Import, "Importing");

    if (kindChangedSpy.count() != 1) {
        fprintf(stderr, "FAIL: kindChanged signal emitted %d times, expected 1\n",
                kindChangedSpy.count());
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

    if (test_set_normal_status()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_import_priority_over_normal()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_error_priority_over_all()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_clear_error_reveals_import()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_clear_import_reveals_normal()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_clear_empty_kind_noop()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_set_empty_text_clears()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_text_changed_signal()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_kind_changed_signal()) {
        ++passed;
    } else {
        ++failed;
    }

    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
