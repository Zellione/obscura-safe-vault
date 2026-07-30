#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <QGuiApplication>
#include <QModelIndex>

#include "vault/vault.h"
#include "gallery_model.h"
#include "test_vault_util.h"

// Helper: create a minimal test vault with a sub-gallery and two images
static vault::Vault createTestVault(const std::string& vault_path)
{
    vault::Vault vault = osvqt_test::createTestVault(vault_path);

    auto result = vault.create_gallery("subfolder");
    if (result != vault::VaultResult::Ok) {
        fprintf(stderr, "Failed to create gallery\n");
        throw std::runtime_error("create_gallery failed");
    }

    osvqt_test::addTinyImages(vault, "image", 2);
    return vault;
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    // Create test vault
    const std::string test_vault_path = "/tmp/osv_qt_gallery_test.osv";
    try {
        vault::Vault vault = createTestVault(test_vault_path);

        // Create gallery model
        GalleryModel model(&vault);

        // Test 1: rowCount at root
        int count = model.rowCount();
        printf("Test 1 (rowCount at root): %d items\n", count);
        if (count != 3) {  // 1 gallery + 2 images
            fprintf(stderr, "FAIL: Expected 3 items, got %d\n", count);
            return 1;
        }
        printf("PASS: rowCount correct (1 gallery + 2 images)\n");

        // Test 2: data roles
        QModelIndex idx0 = model.index(0, 0);
        bool isGallery = model.data(idx0, GalleryModel::IsGalleryRole).toBool();
        QString name = model.data(idx0, GalleryModel::NameRole).toString();
        printf("Test 2 (roles at row 0): name='%s', isGallery=%s\n",
               name.toStdString().c_str(), isGallery ? "true" : "false");
        if (!isGallery || name != "subfolder") {
            fprintf(stderr, "FAIL: First item should be gallery 'subfolder'\n");
            return 1;
        }
        printf("PASS: Galleries appear first\n");

        // Test 3: second item is image
        QModelIndex idx1 = model.index(1, 0);
        bool isImage = !model.data(idx1, GalleryModel::IsGalleryRole).toBool();
        name = model.data(idx1, GalleryModel::NameRole).toString();
        printf("Test 3 (row 1): name='%s', isImage=%s\n",
               name.toStdString().c_str(), isImage ? "true" : "false");
        if (!isImage) {
            fprintf(stderr, "FAIL: Second item should be image\n");
            return 1;
        }
        printf("PASS: Images come after galleries\n");

        // Test 4: roleNames
        auto roles = model.roleNames();
        if (!roles.contains(GalleryModel::NameRole) ||
            !roles.contains(GalleryModel::IsGalleryRole) ||
            !roles.contains(GalleryModel::NodeKeyRole)) {
            fprintf(stderr, "FAIL: Missing expected roles\n");
            return 1;
        }
        printf("PASS: roleNames complete\n");

        // Test 5: currentPath property
        QString path = model.currentPath();
        printf("Test 5 (currentPath): '%s'\n", path.toStdString().c_str());
        if (path != "/") {
            fprintf(stderr, "FAIL: Initial path should be '/'\n");
            return 1;
        }
        printf("PASS: Initial path is '/'\n");

        // Test 6: enterGallery
        model.enterGallery(0);
        path = model.currentPath();
        printf("Test 6 (enterGallery): path='%s'\n", path.toStdString().c_str());
        if (path != "/subfolder") {
            fprintf(stderr, "FAIL: Path should be '/subfolder', got '%s'\n", path.toStdString().c_str());
            return 1;
        }
        printf("PASS: enterGallery navigates correctly\n");

        // Test 7: upOneLevel
        model.upOneLevel();
        path = model.currentPath();
        printf("Test 7 (upOneLevel): path='%s'\n", path.toStdString().c_str());
        if (path != "/") {
            fprintf(stderr, "FAIL: Path should be '/', got '%s'\n", path.toStdString().c_str());
            return 1;
        }
        printf("PASS: upOneLevel navigates correctly\n");

        // Test 8: rename to valid name
        QString originalName = model.data(model.index(1, 0), GalleryModel::NameRole).toString();
        QString newName = "image_renamed";
        QString error = model.rename(1, newName);
        if (!error.isEmpty()) {
            fprintf(stderr, "FAIL: rename should succeed, got error: %s\n", error.toStdString().c_str());
            return 1;
        }
        QString renamedName = model.data(model.index(1, 0), GalleryModel::NameRole).toString();
        printf("Test 8 (rename valid): '%s' -> '%s'\n", originalName.toStdString().c_str(), renamedName.toStdString().c_str());
        if (renamedName != newName) {
            fprintf(stderr, "FAIL: Expected name '%s', got '%s'\n", newName.toStdString().c_str(), renamedName.toStdString().c_str());
            return 1;
        }
        printf("PASS: Valid rename succeeds\n");

        // Tests 9-11: invalid rename attempts must all be rejected
        struct InvalidRenameCase {
            const char* label;
            const char* name;
            const char* what;
        };
        const InvalidRenameCase invalidRenames[] = {
            {"Test 9 (rename invalid)", "image/invalid", "path separators"},
            {"Test 10 (rename empty)", "", "empty name"},
            {"Test 11 (rename '..')", "..", "'..' name"},
        };
        for (const auto& tc : invalidRenames) {
            error = model.rename(1, QString::fromUtf8(tc.name));
            if (error.isEmpty()) {
                fprintf(stderr, "FAIL: rename should reject %s\n", tc.what);
                return 1;
            }
            printf("%s: error='%s'\n", tc.label, error.toStdString().c_str());
            printf("PASS: %s rejected\n", tc.what);
        }

        // Test 12: verify rename persists after vault reopen
        // Reopen vault in a new instance
        vault::Vault vault2;
        std::string pw_str = "test123";
        const std::span<const uint8_t> pw_span2(reinterpret_cast<const uint8_t*>(pw_str.data()), pw_str.size());
        auto open_result = vault::Vault::open(test_vault_path, vault2);
        if (open_result != vault::VaultResult::Ok) {
            fprintf(stderr, "FAIL: Could not reopen vault\n");
            return 1;
        }
        auto unlock_result = vault2.unlock(pw_span2, {});
        if (unlock_result != vault::VaultResult::Ok) {
            fprintf(stderr, "FAIL: Could not unlock reopened vault\n");
            return 1;
        }

        GalleryModel model2(&vault2);
        QString reopenedName = model2.data(model2.index(1, 0), GalleryModel::NameRole).toString();
        printf("Test 12 (rename persistence): after reopen, name='%s'\n", reopenedName.toStdString().c_str());
        if (reopenedName != newName) {
            fprintf(stderr, "FAIL: Expected name '%s' after reopen, got '%s'\n", newName.toStdString().c_str(), reopenedName.toStdString().c_str());
            return 1;
        }
        printf("PASS: Rename persists after vault reopen\n");

        printf("\nAll GalleryModel tests PASSED\n");
        return 0;

    } catch (const std::exception& e) {
        fprintf(stderr, "Test exception: %s\n", e.what());
        return 1;
    }
}
