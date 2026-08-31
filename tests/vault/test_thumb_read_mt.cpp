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

    // Collect node_b's thumbnail chunk ref for read_thumb_span testing.
    const vault::ChunkRef node_b_thumb = vault::media_thumb_chunk_ref(*node_b);

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
                    if (vault::read_thumb_span(v, node_b_thumb, out2)
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
    const vault::ChunkRef node_thumb = vault::media_thumb_chunk_ref(*node);

    // Lock the vault (clears the tree, wipes the master key, sets unlocked_ = false).
    v.lock();

    // read_thumbnail on a copied node should return Locked.
    crypto::SecureBytes out;
    CHECK_EQ(v.read_thumbnail(node_copy, out), vault::VaultResult::Locked);

    // read_thumb_span with a remembered ref should also return Locked.
    crypto::SecureBytes out2;
    CHECK_EQ(vault::read_thumb_span(v, node_thumb, out2), vault::VaultResult::Locked);

    cleanup_dir(dir);
}

// Move assignment operator transfers thumb_fp_ and thumb_mutex_ correctly.
// Phase 58: exercise that moved-to vault can read thumbnails from the moved state.
TEST(vault_move_assignment_preserves_thumb_handle)
{
    const auto dir = fresh_dir("thumb_move_assign");
    vault::Vault v1;
    make_vault(v1, dir / "v1.osv");
    REQUIRE(v1.create_gallery("g") == vault::VaultResult::Ok);

    // Add an image with a real JPEG thumbnail.
    const auto real_jpeg = fixtures::solid_jpeg(8, 8, 0x99, 0xaa, 0xbb);
    REQUIRE(v1.add_image("g", real_jpeg, "move_test.jpg") == vault::VaultResult::Ok);

    auto nodes = v1.list("g");
    REQUIRE(nodes.size() >= 1);
    const auto* node1 = nodes.at(0);
    REQUIRE(node1->meta.thumb_length > 0);

    // Verify we can read the thumbnail before move.
    {
        crypto::SecureBytes out;
        CHECK(v1.read_thumbnail(*node1, out) == vault::VaultResult::Ok);
    }

    // Move-assign v1 to v2 (tests lines 431, 432, 443 in move assignment).
    vault::Vault v2;
    v2 = std::move(v1);

    // After move, v2 should have a valid thumb_fp_ and thumb_mutex_.
    // Re-list from v2 to get the node pointers.
    nodes = v2.list("g");
    REQUIRE(nodes.size() >= 1);
    const auto* node2 = nodes.at(0);
    REQUIRE(node2->meta.thumb_length > 0);

    // Read thumbnail from v2's moved-to state (exercises thumb_fp_ transfer).
    crypto::SecureBytes out2;
    CHECK(v2.read_thumbnail(*node2, out2) == vault::VaultResult::Ok);

    cleanup_dir(dir);
}

// open() path with thumbnail reads exercises the thumb_fp_ initialization
// and error handling in the open() method (lines 597-599).
TEST(vault_open_initializes_thumb_handle_for_reads)
{
    const auto dir = fresh_dir("thumb_open");

    // Phase 1: Create and populate a vault.
    vault::Vault v1;
    make_vault(v1, dir / "test.osv");
    REQUIRE(v1.create_gallery("g") == vault::VaultResult::Ok);

    const auto real_jpeg = fixtures::solid_jpeg(16, 16, 0x42, 0x43, 0x44);
    REQUIRE(v1.add_image("g", real_jpeg, "open_test.jpg") == vault::VaultResult::Ok);

    auto nodes = v1.list("g");
    REQUIRE(nodes.size() >= 1);
    const auto* node_before_open = nodes.at(0);
    REQUIRE(node_before_open->meta.thumb_length > 0);

    const vault::ChunkRef node_ref = vault::media_thumb_chunk_ref(*node_before_open);

    // Read thumbnail to verify it exists.
    {
        crypto::SecureBytes out;
        CHECK(v1.read_thumbnail(*node_before_open, out) == vault::VaultResult::Ok);
    }

    // Lock vault to prepare for re-opening (clearing state).
    v1.lock();

    // Phase 2: Re-open the same vault (exercises open() thumb_fp_ initialization at lines 595-601).
    vault::Vault v2;
    const auto path_str = (dir / "test.osv").string();
    REQUIRE(vault::Vault::open(path_str, v2) == vault::VaultResult::Ok);
    const std::vector<uint8_t> pw{'p', 'w'};
    REQUIRE(v2.unlock(pw, {}) == vault::VaultResult::Ok);

    // Verify that v2's thumb_fp_ is working by reading the same thumbnail.
    nodes = v2.list("g");
    REQUIRE(nodes.size() >= 1);
    const auto* node_after_open = nodes.at(0);

    crypto::SecureBytes out_after;
    CHECK(v2.read_thumbnail(*node_after_open, out_after) == vault::VaultResult::Ok);

    // Also verify read_thumb_span works with the opened vault's thumb_fp_.
    crypto::SecureBytes span_out;
    CHECK(vault::read_thumb_span(v2, node_ref, span_out) == vault::VaultResult::Ok);

    cleanup_dir(dir);
}
