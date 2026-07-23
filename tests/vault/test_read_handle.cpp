#include "test_framework.h"
#include "ui/zip_test_helpers.h"   // ziptest::make_vault, fake_jpeg, fresh_dir, cleanup_dir
#include "vault/vault.h"
#include "vault/staging.h"

#include <thread>

using ziptest::cleanup_dir;
using ziptest::fake_jpeg;
using ziptest::fresh_dir;
using ziptest::make_vault;

// Basic test: read_fp_ is opened and reads work correctly.
TEST(read_handle_basic_read)
{
    const auto dir = fresh_dir("read_handle_basic");
    vault::Vault v;
    make_vault(v, dir / "a.osv");
    REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("g", fake_jpeg(1), "1.jpg") == vault::VaultResult::Ok);
    const auto* node = v.list("g").at(0);

    // Simple read without concurrent writes
    for (int i = 0; i < 10; ++i) {
        crypto::SecureBytes out;
        CHECK(v.read_image(*node, out) == vault::VaultResult::Ok);
        CHECK_EQ(out.size(), fake_jpeg(1).size());
    }
    cleanup_dir(dir);
}

// A read on the main thread must return correct plaintext even while another
// thread is appending chunks (moving the write handle's file position). With a
// single shared FILE* this interleaves fseek/fwrite vs fseek/fread and fails
// (or trips TSAN); with the dedicated read handle it must always pass. Task 2
// uses stage_image (thread-safe staging API) to avoid tree mutations.
TEST(read_handle_survives_concurrent_appends)
{
    const auto dir = fresh_dir("read_handle");
    vault::Vault v;
    make_vault(v, dir / "a.osv");
    REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("g", fake_jpeg(1), "1.jpg") == vault::VaultResult::Ok);
    // Get the node after adding the image (adding invalidates previous list() pointers).
    const auto* node = v.list("g").at(0);

    std::atomic<bool> stop{false};
    std::thread writer([&] {
        // Use stage_image (thread-safe, no tree mutation) to append chunks while
        // the main thread reads. Discard the staged nodes without attaching them.
        int i = 0;
        while (!stop.load()) {
            (void)vault::stage_image(v, fake_jpeg(static_cast<uint8_t>(i % 250)),
                                     "s" + std::to_string(i) + ".jpg");
            ++i;
        }
    });
    for (int i = 0; i < 200; ++i) {
        crypto::SecureBytes out;
        CHECK(v.read_image(*node, out) == vault::VaultResult::Ok);
        CHECK_EQ(out.size(), fake_jpeg(1).size());
    }
    stop.store(true);
    writer.join();
    cleanup_dir(dir);
}
