#include "test_framework.h"

#include "ui/import_queue.h"
#include "ui/zip_test_helpers.h"
#include "image/fixtures.h"
#include "crypto/secure_mem.h"

#include <fstream>
#include <print>

namespace fs = std::filesystem;

// Boundary test: import 25 REAL decodable images (crosses lookahead window = 8).
// Verifies pipeline does NOT stall after 8 items when lookahead_max is hit.
TEST(import_queue_crosses_lookahead_boundary)
{
    const auto temp_dir = ziptest::fresh_dir("test_import_queue_boundary");
    const auto vault_path = temp_dir / "vault.osv";

    // Create vault
    vault::Vault v;
    ziptest::make_vault(v, vault_path);

    // Create 25 REAL decodable PNG files (not fake_jpeg)
    const auto files_dir = temp_dir / "files";
    fs::create_directories(files_dir);
    std::vector<fs::path> files;

    for (int i = 0; i < 25; ++i) {
        const auto path = files_dir / ("image_" + std::to_string(i) + ".png");
        // Generate real tiny PNG (64x64 solid color)
        uint8_t r = static_cast<uint8_t>((i * 10 + 1) & 0xFF);
        uint8_t g = static_cast<uint8_t>((i * 11 + 2) & 0xFF);
        uint8_t b = static_cast<uint8_t>((i * 13 + 3) & 0xFF);
        const auto png_data = fixtures::solid_png(64, 64, r, g, b);
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(png_data.data()),
                                                     static_cast<std::streamsize>(png_data.size()));
        files.push_back(path);
    }

    // Import via ImportQueue
    ui::ImportQueue q;
    q.begin_session(v);
    (void)q.enqueue_files(files, "");

    // Pump until done or timeout (60s should be generous for 25 tiny images)
    auto start = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(60);
    bool timed_out = false;
    while (q.busy() && (std::chrono::steady_clock::now() - start) < timeout) {
        (void)q.drain(0.001);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (std::chrono::steady_clock::now() - start >= timeout) {
        timed_out = true;
    }

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start)
                          .count();

    q.end_session();

    // Verify: all 25 images imported
    auto list = v.list("");
    std::println("[boundary_test] imported {}/25 images in {}ms (timed_out={}), queue_busy={}",
                 list.size(), elapsed_ms, timed_out, q.busy());

    CHECK_EQ(static_cast<int>(list.size()), 25);

    // Verify all are readable
    for (const auto& node : list) {
        REQUIRE(node->is_image());
        crypto::SecureBytes out;
        auto res = v.read_image(*node, out);
        CHECK(res == vault::VaultResult::Ok);
    }

    v.lock();
    ziptest::cleanup_dir(temp_dir);
}
