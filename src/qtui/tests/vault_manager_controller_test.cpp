#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QUrl>

#include "platform/vault_registry.h"
#include "vault_manager_controller.h"
#include "vault_list_model.h"
#include "test_vault_util.h"

namespace fs = std::filesystem;

// Test 1: Controller creates a VaultListModel
static bool test_controller_creates_model()
{
    printf("Test 1: Controller creates a VaultListModel...\n");

    VaultManagerController controller;
    auto* model = controller.vaultListModel();
    if (!model) {
        fprintf(stderr, "FAIL: vaultListModel() returned nullptr\n");
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 2: Registry add/remove round-trip: create a vault, add to registry, remove from registry
static bool test_registry_add_remove()
{
    printf("Test 2: Registry add/remove round-trip...\n");

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        fprintf(stderr, "FAIL: Could not create temp directory\n");
        return false;
    }

    // Create a test vault
    const auto vaultPath = fs::path(tempDir.path().toStdString()) / "test.osv";
    try {
        osvqt_test::createTestVault(vaultPath.string());
    } catch (const std::exception& e) {
        fprintf(stderr, "FAIL: Could not create test vault: %s\n", e.what());
        return false;
    }

    VaultManagerController controller;
    auto* model = controller.vaultListModel();
    if (!model) {
        fprintf(stderr, "FAIL: vaultListModel() returned nullptr\n");
        return false;
    }

    // At this point, the vault list should be empty (it shows vaults from the real registry)
    // Since we created a vault in a temp dir, it won't be in the registry yet.
    const int initialCount = model->rowCount();

    // Test add: programmatically add to registry via vault_manager_controller
    // (We can't test openVaultFromDialog without a real file dialog, so we just
    // test the model refresh behavior.)

    // Create another vault to test removal
    const auto vaultPath2 = fs::path(tempDir.path().toStdString()) / "test2.osv";
    try {
        osvqt_test::createTestVault(vaultPath2.string());
    } catch (const std::exception& e) {
        fprintf(stderr, "FAIL: Could not create second test vault: %s\n", e.what());
        return false;
    }

    // Add both vaults to the registry
    const auto registry = platform::VaultRegistry::default_location();
    registry.add(vaultPath);
    registry.add(vaultPath2);

    // Refresh model and check both are in the list
    model->refresh();
    if (model->rowCount() < 2) {
        fprintf(stderr, "FAIL: Expected at least 2 vaults in model after add, got %d\n",
                model->rowCount());
        return false;
    }

    // Test remove: remove the first vault
    int removeRow = -1;
    for (int i = 0; i < model->rowCount(); ++i) {
        const auto url = model->fileUrlAt(i);
        const auto localPath = url.toLocalFile().toStdString();
        // Compare in generic (forward-slash) form: QUrl::toLocalFile() returns
        // "C:/Users/..." on Windows while path::string() returns the native
        // "C:\Users\...", so a raw string compare never matches there.
        if (fs::path(localPath).generic_string() == vaultPath.generic_string()) {
            removeRow = i;
            break;
        }
    }

    if (removeRow == -1) {
        fprintf(stderr, "FAIL: Could not find first vault in model\n");
        return false;
    }

    if (!controller.removeVaultFromRegistry(removeRow)) {
        fprintf(stderr, "FAIL: removeVaultFromRegistry failed\n");
        return false;
    }

    // Verify removal
    bool found = false;
    for (int i = 0; i < model->rowCount(); ++i) {
        const auto url = model->fileUrlAt(i);
        const auto localPath = url.toLocalFile().toStdString();
        // Compare in generic (forward-slash) form: QUrl::toLocalFile() returns
        // "C:/Users/..." on Windows while path::string() returns the native
        // "C:\Users\...", so a raw string compare never matches there.
        if (fs::path(localPath).generic_string() == vaultPath.generic_string()) {
            found = true;
            break;
        }
    }

    if (found) {
        fprintf(stderr, "FAIL: Vault still in model after remove\n");
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 3: Bad row index returns false
static bool test_remove_bad_row()
{
    printf("Test 3: Remove with bad row index returns false...\n");

    VaultManagerController controller;

    if (controller.removeVaultFromRegistry(-1) == true) {
        fprintf(stderr, "FAIL: removeVaultFromRegistry(-1) should return false\n");
        return false;
    }

    if (controller.removeVaultFromRegistry(1000) == true) {
        fprintf(stderr, "FAIL: removeVaultFromRegistry(1000) should return false\n");
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

    if (test_controller_creates_model()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_registry_add_remove()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_remove_bad_row()) {
        ++passed;
    } else {
        ++failed;
    }

    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
