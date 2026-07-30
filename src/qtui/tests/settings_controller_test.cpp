#include <cstdio>
#include <cstdlib>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtTest/QSignalSpy>

#include "settings_controller.h"

// Helper to set up temp XDG config dir
static QTemporaryDir tempDir;

// Test 1: theme list has 4 items
static bool test_theme_list_count()
{
    printf("Test 1: theme list has 4 items...\n");

    auto controller = std::make_unique<SettingsController>();

    auto themeList = controller->themeList();
    if (themeList.size() != 4) {
        fprintf(stderr, "FAIL: themeList size is %d, expected 4\n",
                static_cast<int>(themeList.size()));
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 2: currentThemeIndex readable
static bool test_current_theme_index()
{
    printf("Test 2: currentThemeIndex readable...\n");

    auto controller = std::make_unique<SettingsController>();

    int idx = controller->currentThemeIndex();
    if (idx < 0 || idx >= 4) {
        fprintf(stderr, "FAIL: currentThemeIndex is %d, expected 0-3\n", idx);
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 3: setCurrentThemeIndex applies and persists (with startup simulation)
static bool test_set_current_theme_index()
{
    printf("Test 3: setCurrentThemeIndex applies and persists...\n");

    // Create a unique temp file for this test
    QTemporaryDir tempTestDir;
    if (!tempTestDir.isValid()) {
        fprintf(stderr, "FAIL: Cannot create temp directory\n");
        return false;
    }

    auto configPath = tempTestDir.filePath("theme.conf");

    // Simulate first app run: create controller, set theme, destroy it
    {
        auto controller = std::make_unique<SettingsController>();
        controller->setThemePersistPath(configPath.toStdString());
        controller->setCurrentThemeIndex(2);
    }

    // Simulate app restart: create new controller (reads persisted theme)
    {
        auto controller = std::make_unique<SettingsController>();
        controller->setThemePersistPath(configPath.toStdString());

        int idx = controller->currentThemeIndex();
        if (idx != 2) {
            fprintf(stderr, "FAIL: theme persisted as %d, expected 2\n", idx);
            return false;
        }
    }

    // Simulate another app restart with different theme
    {
        auto controller = std::make_unique<SettingsController>();
        controller->setThemePersistPath(configPath.toStdString());
        controller->setCurrentThemeIndex(1);
    }

    {
        auto controller = std::make_unique<SettingsController>();
        controller->setThemePersistPath(configPath.toStdString());

        int idx = controller->currentThemeIndex();
        if (idx != 1) {
            fprintf(stderr, "FAIL: theme persisted as %d, expected 1\n", idx);
            return false;
        }
    }

    printf("PASS\n");
    return true;
}

// Test 4: vault-locked state blocks category operations
static bool test_vault_locked_blocks_category_add()
{
    printf("Test 4: vault-locked state blocks category add...\n");

    auto controller = std::make_unique<SettingsController>();
    controller->setVaultUnlocked(false);

    QString err = controller->addCategory("Test Category");
    if (err.isEmpty()) {
        fprintf(stderr, "FAIL: addCategory succeeded when vault locked\n");
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 5: blank category name rejected
static bool test_blank_category_rejected()
{
    printf("Test 5: blank category name rejected...\n");

    auto controller = std::make_unique<SettingsController>();
    controller->setVaultUnlocked(true);

    QString err = controller->addCategory("");
    if (err.isEmpty()) {
        fprintf(stderr, "FAIL: addCategory succeeded with blank name\n");
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 6: category add success returns empty string
static bool test_category_add_success()
{
    printf("Test 6: category add success returns empty string...\n");

    auto controller = std::make_unique<SettingsController>();
    controller->setVaultUnlocked(true);

    QString err = controller->addCategory("New Category");
    if (!err.isEmpty()) {
        fprintf(stderr, "FAIL: addCategory failed with: %s\n",
                err.toStdString().c_str());
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 7: case-insensitive duplicate rejected
static bool test_category_duplicate_rejected()
{
    printf("Test 7: case-insensitive duplicate rejected...\n");

    auto controller = std::make_unique<SettingsController>();
    controller->setVaultUnlocked(true);

    // Add first
    QString err1 = controller->addCategory("Test Category");
    if (!err1.isEmpty()) {
        fprintf(stderr, "FAIL: first addCategory failed: %s\n",
                err1.toStdString().c_str());
        return false;
    }

    // Try to add with different case
    QString err2 = controller->addCategory("test category");
    if (err2.isEmpty()) {
        fprintf(stderr, "FAIL: duplicate (case-insensitive) was accepted\n");
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 8: rename category success
static bool test_rename_category_success()
{
    printf("Test 8: rename category success...\n");

    auto controller = std::make_unique<SettingsController>();
    controller->setVaultUnlocked(true);

    // Add category first
    controller->addCategory("Original");

    // Rename it
    QString err = controller->renameCategory(0, "Renamed");
    if (!err.isEmpty()) {
        fprintf(stderr, "FAIL: renameCategory failed: %s\n",
                err.toStdString().c_str());
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 9: rename to existing name rejected
static bool test_rename_category_duplicate_rejected()
{
    printf("Test 9: rename to existing name rejected...\n");

    auto controller = std::make_unique<SettingsController>();
    controller->setVaultUnlocked(true);

    // Add two categories
    controller->addCategory("First");
    controller->addCategory("Second");

    // Try to rename first to match second (case-insensitive)
    QString err = controller->renameCategory(0, "second");
    if (err.isEmpty()) {
        fprintf(stderr, "FAIL: rename to duplicate was accepted\n");
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 10: remove category success
static bool test_remove_category_success()
{
    printf("Test 10: remove category success...\n");

    auto controller = std::make_unique<SettingsController>();
    controller->setVaultUnlocked(true);

    // Add category
    controller->addCategory("To Remove");

    // Remove it
    controller->removeCategory(0);

    // Verify categories list is now empty
    auto cats = controller->categories();
    if (cats.size() != 0) {
        fprintf(stderr, "FAIL: categories still has %d items after remove\n",
                static_cast<int>(cats.size()));
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 11: errorLine cleared on successful operation
static bool test_error_line_cleared()
{
    printf("Test 11: errorLine cleared on successful operation...\n");

    auto controller = std::make_unique<SettingsController>();
    controller->setVaultUnlocked(true);

    // Trigger an error
    controller->addCategory("");
    if (controller->errorLine().isEmpty()) {
        fprintf(stderr, "FAIL: errorLine not set after error\n");
        return false;
    }

    // Successful operation should clear it
    controller->addCategory("Valid Name");
    if (!controller->errorLine().isEmpty()) {
        fprintf(stderr, "FAIL: errorLine not cleared after success: %s\n",
                controller->errorLine().toStdString().c_str());
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

    if (test_theme_list_count()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_current_theme_index()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_set_current_theme_index()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_vault_locked_blocks_category_add()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_blank_category_rejected()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_category_add_success()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_category_duplicate_rejected()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_rename_category_success()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_rename_category_duplicate_rejected()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_remove_category_success()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_error_line_cleared()) {
        ++passed;
    } else {
        ++failed;
    }

    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
