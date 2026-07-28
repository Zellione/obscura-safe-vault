#include "test_framework.h"
#include "ui/zip_test_helpers.h"   // ziptest::make_vault, fresh_dir, cleanup_dir
#include "image/fixtures.h"        // fixtures::solid_jpeg
#include "vault/vault.h"

#include <thread>

using ziptest::cleanup_dir;
using ziptest::fresh_dir;
using ziptest::make_vault;

// Concurrent thumbnail reads from multiple threads are thread-safe. Four worker threads
// hammer read_thumbnail and read_thumb_span on different images while the main thread
// does read_image, ensuring the dedicated thumb_fp_ + thumb_mutex_ handles concurrent access.
TEST(thumbnail_reads_are_thread_safe)
{
    const auto dir = fresh_dir("thumb_read_mt");
    vault::Vault v;
    make_vault(v, dir / "a.osv");
    REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);

    // Add two images using a real decodable JPEG so thumbnails are actually generated.
    const auto real_jpeg_1 = fixtures::solid_jpeg(8, 8, 0x11, 0x22, 0x33);
    const auto real_jpeg_2 = fixtures::solid_jpeg(8, 8, 0xaa, 0xbb, 0xcc);
    REQUIRE(v.add_image("g", real_jpeg_1, "img1.jpg") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("g", real_jpeg_2, "img2.jpg") == vault::VaultResult::Ok);

    // Get the nodes after adding images (adding invalidates previous list() pointers).
    auto nodes = v.list("g");
    REQUIRE(nodes.size() >= 2);
    const auto* node_a = nodes.at(0);
    const auto* node_b = nodes.at(1);

    // Verify thumbnails were actually generated and stored.
    REQUIRE(node_a->meta.thumb_length > 0);
    REQUIRE(node_b->meta.thumb_length > 0);

    // Collect thumbnail spans from node_b for read_thumb_span testing.
    const uint64_t span_b_off = node_b->meta.thumb_offset;
    const uint64_t span_b_len = node_b->meta.thumb_length;

    // Basic test: can we read thumbnails?
    {
        crypto::SecureBytes out;
        CHECK(v.read_thumbnail(*node_a, out) == vault::VaultResult::Ok);
    }

    std::atomic<int> failures{0};
    {
        // Spawn 4 worker threads that hammer thumbnail reads.
        std::vector<std::jthread> workers;
        for (int t = 0; t < 4; ++t) {
            workers.emplace_back([&] {
                for (int i = 0; i < 25; ++i) {
                    crypto::SecureBytes out;
                    if (v.read_thumbnail(*node_a, out) != vault::VaultResult::Ok) {
                        ++failures;
                    }
                    crypto::SecureBytes out2;
                    if (vault::read_thumb_span(v, span_b_off, span_b_len, out2)
                        != vault::VaultResult::Ok) {
                        ++failures;
                    }
                }
            });
        }
        // Main thread does read_image on the same node while workers read thumbnails.
        for (int i = 0; i < 25; ++i) {
            crypto::SecureBytes img;
            if (v.read_image(*node_a, img) != vault::VaultResult::Ok) {
                ++failures;
            }
        }
    }
    CHECK_EQ(failures.load(), 0);
    cleanup_dir(dir);
}

// Thumbnail reads return Locked when the vault is locked. After lock(), any
// in-flight thumbnail read should observe unlocked_ == false.
TEST(thumbnail_read_after_lock_returns_locked)
{
    const auto dir = fresh_dir("thumb_read_locked");
    vault::Vault v;
    make_vault(v, dir / "a.osv");
    REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);

    // Add an image using a real decodable JPEG so a thumbnail is actually generated.
    const auto real_jpeg = fixtures::solid_jpeg(8, 8, 0x55, 0x66, 0x77);
    REQUIRE(v.add_image("g", real_jpeg, "locked_img.jpg") == vault::VaultResult::Ok);

    // Get the node and verify it has a thumbnail.
    auto nodes = v.list("g");
    REQUIRE(nodes.size() >= 1);
    const auto* node = nodes.at(0);
    REQUIRE(node->meta.thumb_length > 0);

    // Read thumbnail while unlocked to verify it works.
    {
        crypto::SecureBytes out;
        CHECK(v.read_thumbnail(*node, out) == vault::VaultResult::Ok);
    }

    // Copy the node data (it is a value struct) because lock() will clear the tree.
    const auto node_copy = *node;
    const uint64_t span_off = node->meta.thumb_offset;
    const uint64_t span_len = node->meta.thumb_length;

    // Lock the vault (clears the tree, wipes the master key, sets unlocked_ = false).
    v.lock();

    // read_thumbnail on a copied node should return Locked.
    crypto::SecureBytes out;
    CHECK_EQ(v.read_thumbnail(node_copy, out), vault::VaultResult::Locked);

    // read_thumb_span with remembered offsets should also return Locked.
    crypto::SecureBytes out2;
    CHECK_EQ(vault::read_thumb_span(v, span_off, span_len, out2), vault::VaultResult::Locked);

    cleanup_dir(dir);
}
