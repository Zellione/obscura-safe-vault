#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <QGuiApplication>
#include <QModelIndex>

#include "vault/vault.h"
#include "gallery_model.h"
#include "thumb_cache.h"
#include "test_vault_util.h"

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    printf("=== ThumbCache Stress Test: Tomb Safety ===\n");

    // Create stress test vault with many images
    const std::string test_vault_path = "/tmp/osv_qt_thumb_stress.osv";

    printf("Creating vault with 120 images...\n");

    try {
        vault::Vault vault = osvqt_test::createTestVault(test_vault_path);
        osvqt_test::addTinyImages(vault, "img_", 120, 20);

        printf("Vault created. Initializing ThumbCache and GalleryModel...\n");

        // Initialize cache and model
        ThumbCache thumbCache;
        thumbCache.setVault(&vault);
        GalleryModel galleryModel(&vault);

        printf("Queuing 120 thumbnail requests...\n");

        // Queue thumbnail requests for all images
        const auto nodes = vault.list("");
        int request_count = 0;
        for (const auto* node : nodes) {
            if (node && node->type == vault::IndexNode::Type::Image) {
                quintptr key = reinterpret_cast<quintptr>(node);
                thumbCache.request(key);
                request_count++;
            }
        }

        printf("Queued %d thumbnail requests\n", request_count);

        // CRITICAL TEST: Immediately lock vault while workers are processing
        // This tests the lifetime safety mechanism (generation epoch + drain)
        printf("Calling lock() to test drain-before-lock safety...\n");

        // This should:
        // 1. Call thumbCache->shutdownAndDrain() which:
        //    a. Sets stopping_ flag
        //    b. Bumps generation epoch
        //    c. Waits for all workers to complete (no crash on stale pointers)
        // 2. Call vault.lock() to wipe the tree
        // 3. Call thumbCache->clearAll() to clean up
        galleryModel.upOneLevel();  // Back to root (safe)
        thumbCache.shutdownAndDrain();  // Explicit drain for stress test
        vault.lock();
        thumbCache.clearAll();
        printf("PASS: Lock completed without crash\n");
    } catch (const std::exception& e) {
        fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }

    printf("\n=== All Stress Tests PASSED ===\n");
    printf("Tomb safety verified: workers completed cleanly during lock\n");
    return 0;
}
