#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#include <filesystem>
#include <fstream>
#include <random>

#include <QGuiApplication>
#include <QModelIndex>

#include "vault/vault.h"
#include "gallery_model.h"
#include "crypto/kdf.h"

namespace fs = std::filesystem;

// Helper: create a minimal test vault with galleries and images
static vault::Vault createTestVault(const std::string& vault_path)
{
    // Remove old vault if it exists
    if (fs::exists(vault_path)) {
        fs::remove(vault_path);
    }

    // Create a simple password and keyfile (empty)
    std::string password = "test123";
    const std::span<const uint8_t> pw_span(reinterpret_cast<const uint8_t*>(password.data()), password.size());
    const std::span<const uint8_t> keyfile_span{};

    vault::Vault vault;
    // Use test-speed KdfParams (same as tests/vault/test_vault.cpp)
    crypto::KdfParams kdf_params{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};
    auto result = vault::Vault::create(vault_path, pw_span, keyfile_span, kdf_params, vault);
    if (result != vault::VaultResult::Ok) {
        fprintf(stderr, "Failed to create test vault\n");
        throw std::runtime_error("create vault failed");
    }

    // Vault is already unlocked after create, so no need to unlock

    // Create a gallery
    result = vault.create_gallery("subfolder");
    if (result != vault::VaultResult::Ok) {
        fprintf(stderr, "Failed to create gallery\n");
        throw std::runtime_error("create_gallery failed");
    }

    // Create a small test image (1x1 white pixel, JPEG format)
    // This is a minimal valid JPEG
    const uint8_t tiny_jpeg[] = {
        0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46,
        0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01,
        0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43,
        0x00, 0x08, 0x06, 0x06, 0x07, 0x06, 0x05, 0x08,
        0x07, 0x07, 0x07, 0x09, 0x09, 0x08, 0x0A, 0x0C,
        0x14, 0x0D, 0x0C, 0x0B, 0x0B, 0x0C, 0x19, 0x12,
        0x13, 0x0F, 0x14, 0x1D, 0x1A, 0x1F, 0x1E, 0x1D,
        0x1A, 0x1C, 0x1C, 0x20, 0x24, 0x2E, 0x27, 0x20,
        0x22, 0x2C, 0x23, 0x1C, 0x1C, 0x28, 0x37, 0x29,
        0x2C, 0x30, 0x31, 0x34, 0x34, 0x34, 0x1F, 0x27,
        0x39, 0x3D, 0x38, 0x32, 0x3C, 0x2E, 0x33, 0x34,
        0x32, 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x01,
        0x00, 0x01, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xC4,
        0x00, 0x1F, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0xFF,
        0xC4, 0x00, 0xB5, 0x10, 0x00, 0x02, 0x01, 0x03,
        0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04,
        0x00, 0x00, 0x01, 0x7D, 0x01, 0x02, 0x03, 0x00,
        0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06,
        0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32,
        0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1,
        0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72,
        0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A,
        0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35,
        0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45,
        0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55,
        0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65,
        0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75,
        0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85,
        0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94,
        0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3,
        0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2,
        0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA,
        0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9,
        0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8,
        0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6,
        0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4,
        0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFF, 0xDA,
        0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3F, 0x00,
        0xFB, 0xD3, 0xFF, 0xD9
    };

    // Add images to root gallery
    for (int i = 0; i < 2; ++i) {
        std::string img_name = std::string("image") + std::to_string(i + 1);
        std::vector<uint8_t> image_data(tiny_jpeg, tiny_jpeg + sizeof(tiny_jpeg));
        const std::span<const uint8_t> img_span(image_data.data(), image_data.size());
        result = vault.add_image("", img_span, img_name);  // "" = root gallery
        if (result != vault::VaultResult::Ok) {
            fprintf(stderr, "Failed to add image %d\n", i);
            throw std::runtime_error("add_image failed");
        }
    }

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

        printf("\nAll GalleryModel tests PASSED\n");
        return 0;

    } catch (const std::exception& e) {
        fprintf(stderr, "Test exception: %s\n", e.what());
        return 1;
    }
}
