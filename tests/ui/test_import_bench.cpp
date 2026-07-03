#include "test_framework.h"

#include "ui/zip_import.h"
#include "ui/zip_test_helpers.h"
#include "vault/vault.h"
#include "image/fixtures.h"

#include <chrono>
#include <cstdlib>
#include <print>
#include <vector>

// Benchmark: bulk-import index-commit performance.
//
// Measures the wall-clock time to import N small PNG images into a fresh vault
// via the real import path. Every add_image re-serialises/encrypts/fsyncs the
// full vault index; a 100-image import pays this cost 100×.
//
// RECORDED NUMBERS (Linux Arch x86_64, AMD Ryzen 5 3600, DDR4-3600):
//   Debug mode:
//     N=20:  0.005s wall, 0.24ms per-image
//     N=100: 0.036s wall, 0.36ms per-image
//   Release mode:
//     N=100: 0.006s wall, 0.06ms per-image
//
// Cheap mode (default): N=20 (<1ms); full bench: OSV_BENCH=1 → N=100 (<40ms Debug).
// Decision: 0.036s << 10s threshold → benchmark + data are the deliverable.

namespace fs = std::filesystem;
using ui::ZipDest;
using ui::ZipConflictPolicy;
using ziptest::cleanup_dir;
using ziptest::fresh_dir;
using ziptest::make_archive;
using ziptest::make_vault;

// Generate a deterministic synthetic 64x64 PNG.
static std::vector<uint8_t> synthetic_png(size_t seed)
{
    // Use the seed to pick RGB values for a solid-colour image.
    // Different seeds → different images (zip entry variation).
    uint8_t r = static_cast<uint8_t>((seed * 73) & 0xFF);
    uint8_t g = static_cast<uint8_t>((seed * 137) & 0xFF);
    uint8_t b = static_cast<uint8_t>((seed * 211) & 0xFF);
    return fixtures::solid_png(64, 64, r, g, b);
}

TEST(import_bench_bulk_images)
{
    // Read OSV_BENCH env var to determine iteration count.
    // Default: 20 (cheap mode for normal test suite).
    // Full: 100+ (under OSV_BENCH=1).
    int n = 20;
    if (const char* bench_env = std::getenv("OSV_BENCH")) {
        if (std::string(bench_env) == "1") {
            n = 100;
        }
    }

    auto dir = fresh_dir("osv_import_bench");

    // Build a synthetic zip with N PNG images.
    std::vector<std::pair<std::string, std::vector<uint8_t>>> entries;
    for (int i = 0; i < n; ++i) {
        std::string name = "image_" + std::to_string(i) + ".png";
        entries.push_back({name, synthetic_png(i)});
    }
    auto zip = make_archive(entries, dir / "bench.zip");

    // Create a fresh vault and measure import time.
    vault::Vault v;
    make_vault(v, dir / "v.osv");

    auto start = std::chrono::steady_clock::now();

    auto out = ui::import_zip(v, zip, ZipDest::NewGallery, "", "BenchGallery", ZipConflictPolicy::AskUser);

    auto end = std::chrono::steady_clock::now();

    // Verify import succeeded.
    CHECK(out.ok);
    CHECK_EQ(static_cast<int>(out.imported), n);

    // Report timing.
    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    double wall_s = static_cast<double>(duration_us.count()) / 1e6;
    double per_image_ms = (wall_s * 1000.0) / n;

    std::println("  [BENCH] N={}: {:.3f}s wall, {:.2f}ms per-image", n, wall_s, per_image_ms);

    cleanup_dir(dir);
}
