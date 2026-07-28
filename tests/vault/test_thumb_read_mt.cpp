#include "test_framework.h"
#include "ui/zip_test_helpers.h"   // ziptest::make_vault, fake_jpeg, fresh_dir, cleanup_dir
#include "vault/vault.h"

#include <thread>

using ziptest::cleanup_dir;
using ziptest::fake_jpeg;
using ziptest::fresh_dir;
using ziptest::make_vault;

// Basic thumbnail read test: verify read_thumbnail works on a freshly added image.
TEST(thumbnail_reads_are_thread_safe)
{
    const auto dir = fresh_dir("thumb_read_mt");
    vault::Vault v;
    make_vault(v, dir / "a.osv");
    REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);

    // Add two images so we have thumbnails to read.
    REQUIRE(v.add_image("g", fake_jpeg(1), "img1.jpg") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("g", fake_jpeg(2), "img2.jpg") == vault::VaultResult::Ok);

    // Get the nodes after adding images (adding invalidates previous list() pointers).
    auto nodes = v.list("g");
    REQUIRE(nodes.size() >= 2);
    const auto* node_a = nodes.at(0);
    const auto* node_b = nodes.at(1);

    // Collect thumbnail spans from node_b for read_thumb_span testing.
    const uint64_t span_b_off = node_b->is_video() ? node_b->vmeta.poster_offset : node_b->meta.thumb_offset;
    const uint64_t span_b_len = node_b->is_video() ? node_b->vmeta.poster_length : node_b->meta.thumb_length;

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
    REQUIRE(v.add_image("g", fake_jpeg(1), "locked_img.jpg") == vault::VaultResult::Ok);

    // Get the node and read a thumbnail to verify it works while unlocked.
    auto nodes = v.list("g");
    REQUIRE(nodes.size() >= 1);
    const auto* node = nodes.at(0);
    {
        crypto::SecureBytes out;
        auto result = v.read_thumbnail(*node, out);
        CHECK(result == vault::VaultResult::Ok);
    }

    // Copy the node data (it is a value struct) because lock() will clear the tree.
    const auto node_copy = *node;
    const uint64_t span_off = node->is_video() ? node->vmeta.poster_offset : node->meta.thumb_offset;
    const uint64_t span_len = node->is_video() ? node->vmeta.poster_length : node->meta.thumb_length;

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
