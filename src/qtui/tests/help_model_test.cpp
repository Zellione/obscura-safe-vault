#include <cstdio>
#include <QGuiApplication>
#include <QVariantList>
#include <QVariantMap>

#include "help_model.h"

// Test 1: Global group synthesized and prepended
static bool test_global_group_synthesized()
{
    printf("Test 1: Global group synthesized...\n");

    auto model = std::make_unique<HelpModel>();

    auto groups = model->groups();

    // Should have at least the Global group
    if (groups.isEmpty()) {
        fprintf(stderr, "FAIL: groups list is empty\n");
        return false;
    }

    // Check first group is Global
    QVariantMap firstGroup = groups.at(0).toMap();
    QString title = firstGroup.value("title").toString();

    if (title != "Global") {
        fprintf(stderr, "FAIL: first group title is '%s', expected 'Global'\n",
                title.toStdString().c_str());
        return false;
    }

    // Global should have entries
    QVariantList entries = firstGroup.value("entries").toList();
    if (entries.isEmpty()) {
        fprintf(stderr, "FAIL: Global group has no entries\n");
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 2: Global group contains F1, F2, Right-click entries
static bool test_global_group_entries()
{
    printf("Test 2: Global group contains F1, F2, Right-click entries...\n");

    auto model = std::make_unique<HelpModel>();

    auto groups = model->groups();
    QVariantMap firstGroup = groups.at(0).toMap();
    QVariantList entries = firstGroup.value("entries").toList();

    // Look for F1, F2, and Right-click entries
    bool hasF1 = false;
    bool hasF2 = false;
    bool hasRightClick = false;

    for (const auto& entry : entries) {
        QVariantMap entryMap = entry.toMap();
        QString keys = entryMap.value("keys").toString();

        if (keys.contains("F1")) {
            hasF1 = true;
        }
        if (keys.contains("F2")) {
            hasF2 = true;
        }
        if (keys.contains("Right-click") || keys.contains("Right")) {
            hasRightClick = true;
        }
    }

    if (!hasF1) {
        fprintf(stderr, "FAIL: Global group missing F1 entry\n");
        return false;
    }
    if (!hasF2) {
        fprintf(stderr, "FAIL: Global group missing F2 entry\n");
        return false;
    }
    if (!hasRightClick) {
        fprintf(stderr, "FAIL: Global group missing Right-click entry\n");
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 3: Screen groups appear after Global group
static bool test_screen_groups_after_global()
{
    printf("Test 3: Screen groups appear after Global group...\n");

    auto model = std::make_unique<HelpModel>();

    // Create some screen groups
    QVariantList screenGroups;

    QVariantMap group1;
    group1.insert("title", "Navigation");
    QVariantList entries1;
    QVariantMap entry1;
    entry1.insert("keys", "Arrow Keys");
    entry1.insert("description", "Navigate");
    entries1.append(entry1);
    group1.insert("entries", entries1);
    screenGroups.append(group1);

    QVariantMap group2;
    group2.insert("title", "Actions");
    QVariantList entries2;
    QVariantMap entry2;
    entry2.insert("keys", "Enter");
    entry2.insert("description", "Confirm");
    entries2.append(entry2);
    group2.insert("entries", entries2);
    screenGroups.append(group2);

    model->setScreenGroups(screenGroups);

    auto allGroups = model->groups();

    // Should have Global + 2 screen groups = 3 total
    if (allGroups.size() != 3) {
        fprintf(stderr, "FAIL: groups count is %d, expected 3\n", allGroups.size());
        return false;
    }

    // First should be Global
    if (allGroups.at(0).toMap().value("title").toString() != "Global") {
        fprintf(stderr, "FAIL: first group is not Global\n");
        return false;
    }

    // Second should be Navigation
    if (allGroups.at(1).toMap().value("title").toString() != "Navigation") {
        fprintf(stderr, "FAIL: second group is not Navigation, got '%s'\n",
                allGroups.at(1).toMap().value("title").toString().toStdString().c_str());
        return false;
    }

    // Third should be Actions
    if (allGroups.at(2).toMap().value("title").toString() != "Actions") {
        fprintf(stderr, "FAIL: third group is not Actions, got '%s'\n",
                allGroups.at(2).toMap().value("title").toString().toStdString().c_str());
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 4: Empty screen groups results in only Global
static bool test_empty_screen_groups()
{
    printf("Test 4: Empty screen groups results in only Global...\n");

    auto model = std::make_unique<HelpModel>();

    // Set empty screen groups
    QVariantList emptyGroups;
    model->setScreenGroups(emptyGroups);

    auto allGroups = model->groups();

    // Should have only Global
    if (allGroups.size() != 1) {
        fprintf(stderr, "FAIL: groups count is %d, expected 1\n", allGroups.size());
        return false;
    }

    if (allGroups.at(0).toMap().value("title").toString() != "Global") {
        fprintf(stderr, "FAIL: only group is not Global\n");
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 5: setScreenGroups updates the groups list
static bool test_set_screen_groups_updates()
{
    printf("Test 5: setScreenGroups updates the groups list...\n");

    auto model = std::make_unique<HelpModel>();

    QVariantList screenGroups;
    QVariantMap group;
    group.insert("title", "Test Group");
    QVariantList entries;
    QVariantMap entry;
    entry.insert("keys", "T");
    entry.insert("description", "Test");
    entries.append(entry);
    group.insert("entries", entries);
    screenGroups.append(group);

    model->setScreenGroups(screenGroups);

    auto allGroups = model->groups();

    // Should have Global + Test Group = 2 total
    if (allGroups.size() != 2) {
        fprintf(stderr, "FAIL: groups count is %d, expected 2\n", allGroups.size());
        return false;
    }

    // Update with different groups
    QVariantList newScreenGroups;
    QVariantMap newGroup;
    newGroup.insert("title", "New Group");
    QVariantList newEntries;
    QVariantMap newEntry;
    newEntry.insert("keys", "N");
    newEntry.insert("description", "New");
    newEntries.append(newEntry);
    newGroup.insert("entries", newEntries);
    newScreenGroups.append(newGroup);

    model->setScreenGroups(newScreenGroups);

    auto updatedGroups = model->groups();

    // Should still have 2 groups
    if (updatedGroups.size() != 2) {
        fprintf(stderr, "FAIL: groups count after update is %d, expected 2\n", updatedGroups.size());
        return false;
    }

    // Second group should be New Group
    if (updatedGroups.at(1).toMap().value("title").toString() != "New Group") {
        fprintf(stderr, "FAIL: updated group title is '%s', expected 'New Group'\n",
                updatedGroups.at(1).toMap().value("title").toString().toStdString().c_str());
        return false;
    }

    printf("PASS\n");
    return true;
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    qRegisterMetaType<QString>("QString");
    qRegisterMetaType<QVariantList>("QVariantList");
    qRegisterMetaType<QVariantMap>("QVariantMap");

    int passed = 0;
    int failed = 0;

    if (test_global_group_synthesized()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_global_group_entries()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_screen_groups_after_global()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_empty_screen_groups()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_set_screen_groups_updates()) {
        ++passed;
    } else {
        ++failed;
    }

    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
