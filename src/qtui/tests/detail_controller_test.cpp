#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include <QGuiApplication>

#include "vault/vault.h"
#include "detail_controller.h"
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
    // temp_directory_path(), not a hardcoded "/tmp/...": Windows has no /tmp,
    // so the vault create would fail there at runtime.
    const std::string test_vault_path =
        (std::filesystem::temp_directory_path() / "osv_qt_detail_controller_test.osv").string();
    try {
        vault::Vault vault = createTestVault(test_vault_path);

        // Test 1: Initial state is empty
        printf("Test 1 (Initial state): DetailController empty state\n");
        DetailController detail(&vault);
        if (!detail.heading().isEmpty()) {
            fprintf(stderr, "FAIL: Initial heading should be empty\n");
            return 1;
        }
        if (detail.sectionCount() != 0) {
            fprintf(stderr, "FAIL: Initial section count should be 0\n");
            return 1;
        }
        printf("PASS: Initial state is empty\n");

        // Get the nodes to work with
        auto nodes = vault.list("/");
        if (nodes.size() < 3) {
            fprintf(stderr, "FAIL: Expected at least 3 nodes\n");
            return 1;
        }

        // Find the gallery node
        const vault::IndexNode* gallery_node = nullptr;
        for (const auto* node : nodes) {
            if (node->type == vault::IndexNode::Type::Gallery) {
                gallery_node = node;
                break;
            }
        }
        if (!gallery_node) {
            fprintf(stderr, "FAIL: No gallery node found\n");
            return 1;
        }

        const vault::IndexNode* image_node = nullptr;
        for (const auto* node : nodes) {
            if (node->type == vault::IndexNode::Type::Image) {
                image_node = node;
                break;
            }
        }
        if (!image_node) {
            fprintf(stderr, "FAIL: No image node found\n");
            return 1;
        }

        // Test 2: showNode sets heading (gallery)
        printf("Test 2 (showNode with gallery): Heading and section count\n");
        quintptr gallery_key = reinterpret_cast<quintptr>(gallery_node);
        detail.showNode(gallery_key, QStringList(), QStringList());

        QString heading = detail.heading();
        printf("  Heading: '%s'\n", heading.toStdString().c_str());
        if (heading.isEmpty() || heading != "subfolder") {
            fprintf(stderr, "FAIL: Expected heading 'subfolder', got '%s'\n", heading.toStdString().c_str());
            return 1;
        }

        int section_count = detail.sectionCount();
        printf("  Section count: %d\n", section_count);
        if (section_count <= 0) {
            fprintf(stderr, "FAIL: Gallery should have at least one section\n");
            return 1;
        }
        printf("PASS: showNode sets heading and sections\n");

        // Test 3: showNode with image
        printf("Test 3 (showNode with image): Heading and content\n");
        quintptr image_key = reinterpret_cast<quintptr>(image_node);
        detail.showNode(image_key, QStringList(), QStringList());

        heading = detail.heading();
        printf("  Heading: '%s'\n", heading.toStdString().c_str());
        if (heading.isEmpty()) {
            fprintf(stderr, "FAIL: Image should have a heading\n");
            return 1;
        }
        printf("PASS: showNode works with images\n");

        // Test 4: showSelection with two images (multi-select aggregation)
        printf("Test 4 (showSelection multi-select): Aggregate heading and counts\n");
        QList<quintptr> selected;
        int image_count = 0;
        for (const auto* node : nodes) {
            if (node->type == vault::IndexNode::Type::Image && image_count < 2) {
                selected.append(reinterpret_cast<quintptr>(node));
                image_count++;
            }
        }

        if (selected.size() < 2) {
            fprintf(stderr, "FAIL: Expected at least 2 images for multi-select test\n");
            return 1;
        }

        detail.showSelection(selected, QStringList());
        heading = detail.heading();
        printf("  Heading: '%s'\n", heading.toStdString().c_str());

        // Should say "2 items selected"
        if (!heading.contains("2") || !heading.contains("selected")) {
            fprintf(stderr, "FAIL: Expected '2 items selected' heading, got '%s'\n", heading.toStdString().c_str());
            return 1;
        }
        printf("PASS: showSelection aggregates heading correctly\n");

        // Test 5: totalHeight is positive after content
        printf("Test 5 (totalHeight): Content height calculation\n");
        detail.showNode(gallery_key, QStringList(), QStringList());
        float height = detail.totalHeight();
        printf("  Total height: %.1f px\n", height);
        if (height <= 0.0f) {
            fprintf(stderr, "FAIL: Expected positive height, got %.1f\n", height);
            return 1;
        }
        printf("PASS: totalHeight is positive after showNode\n");

        // Test 6: clear() resets state
        printf("Test 6 (clear): Reset to empty state\n");
        detail.clear();
        if (!detail.heading().isEmpty()) {
            fprintf(stderr, "FAIL: After clear(), heading should be empty\n");
            return 1;
        }
        if (detail.sectionCount() != 0) {
            fprintf(stderr, "FAIL: After clear(), section count should be 0\n");
            return 1;
        }
        printf("PASS: clear() resets state\n");

        printf("\nAll DetailController tests PASSED\n");
        return 0;

    } catch (const std::exception& e) {
        fprintf(stderr, "Test exception: %s\n", e.what());
        return 1;
    }
}
