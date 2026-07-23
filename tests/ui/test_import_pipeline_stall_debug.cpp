#include "test_framework.h"

#include "ui/import_queue.h"
#include "ui/zip_test_helpers.h"
#include "image/fixtures.h"
#include "crypto/secure_mem.h"

#include <fstream>
#include <print>
#include <thread>

namespace fs = std::filesystem;

// Debug test: identify WHERE the pipeline stalls after 8 files
// by checking intermediate queue states periodically
TEST(import_pipeline_stall_site_identified)
{
    const auto temp_dir = ziptest::fresh_dir("test_pipeline_stall_debug");
    const auto vault_path = temp_dir / "vault.osv";

    vault::Vault v;
    ziptest::make_vault(v, vault_path);

    // Create 20 REAL decodable PNG files
    const auto files_dir = temp_dir / "files";
    fs::create_directories(files_dir);
    std::vector<fs::path> files;

    for (int i = 0; i < 20; ++i) {
        const auto path = files_dir / ("image_" + std::to_string(i) + ".png");
        uint8_t r = static_cast<uint8_t>((i * 10 + 1) & 0xFF);
        uint8_t g = static_cast<uint8_t>((i * 11 + 2) & 0xFF);
        uint8_t b = static_cast<uint8_t>((i * 13 + 3) & 0xFF);
        const auto png_data = fixtures::solid_png(64, 64, r, g, b);
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(png_data.data()),
                                                     static_cast<std::streamsize>(png_data.size()));
        files.push_back(path);
    }

    ui::ImportQueue q;
    q.begin_session(v);
    (void)q.enqueue_files(files, "");

    // Sample queue state every 500ms to track progress
    std::vector<std::pair<int, int>> sample_log;  // (elapsed_ms, imported_count)

    auto start = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(30);

    while (q.busy() && (std::chrono::steady_clock::now() - start) < timeout) {
        (void)q.drain(0.001);

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);

        // Sample every 500ms
        if (elapsed.count() % 500 < 10) {
            auto list = v.list("");
            sample_log.push_back({static_cast<int>(elapsed.count()), static_cast<int>(list.size())});
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    q.end_session();

    auto final_list = v.list("");
    auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    std::println("[stall_debug] Import sequence:");
    int last_count = 0;
    for (const auto& [ms, count] : sample_log) {
        if (count != last_count) {
            std::println("  {}ms: {} imported", ms, count);
            last_count = count;
        }
    }
    std::println("  FINAL: {} imported in {}ms, queue_busy={}", final_list.size(), total_elapsed.count(), q.busy());

    // GATE: must import AT LEAST file #8 (should cross the lookahead boundary)
    // If stalled at 8, this will fail and indicate the problem
    CHECK(static_cast<int>(final_list.size()) > 8);

    v.lock();
    ziptest::cleanup_dir(temp_dir);
}
