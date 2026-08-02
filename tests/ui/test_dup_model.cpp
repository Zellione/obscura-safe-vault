#include "test_framework.h"

#include <vector>

#include "ui/dup_model.h"

// --- dhash64 ---------------------------------------------------------------

// Solid-colour image: every 9x8 cell equal -> no "left < right" edge -> hash 0.
TEST(dup_dhash_solid_image_is_zero)
{
    std::vector<uint8_t> rgb(32 * 32 * 3, 128);
    CHECK_EQ(ui::dhash64(rgb, 32, 32), uint64_t{0});
}

// Horizontal left-to-right brightness ramp: every left cell darker than its
// right neighbour -> all 64 bits set.
TEST(dup_dhash_horizontal_ramp_is_all_ones)
{
    const int w = 90, h = 8;
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const auto v = static_cast<uint8_t>(x * 255 / (w - 1));
            uint8_t* p = &rgb[(static_cast<size_t>(y) * w + x) * 3];
            p[0] = p[1] = p[2] = v;
        }
    CHECK_EQ(ui::dhash64(rgb, w, h), ~uint64_t{0});
}

// Same picture at two scales -> same hash (that is the point of dHash).
TEST(dup_dhash_scale_invariant_for_ramp)
{
    auto ramp = [](int w, int h) {
        std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const auto v = static_cast<uint8_t>(x * 255 / (w - 1));
                uint8_t* p = &rgb[(static_cast<size_t>(y) * w + x) * 3];
                p[0] = p[1] = p[2] = v;
            }
        return rgb;
    };
    const auto small = ramp(18, 16);
    const auto big   = ramp(180, 160);
    CHECK_EQ(ui::dhash64(small, 18, 16), ui::dhash64(big, 180, 160));
}

// 1x1 image must not crash and hashes to 0 (no gradient information).
TEST(dup_dhash_one_pixel_is_zero)
{
    const std::vector<uint8_t> rgb{10, 20, 30};
    CHECK_EQ(ui::dhash64(rgb, 1, 1), uint64_t{0});
}

// --- hamming64 -------------------------------------------------------------

TEST(dup_hamming_counts_differing_bits)
{
    CHECK_EQ(ui::hamming64(0, 0), 0);
    CHECK_EQ(ui::hamming64(0xFF, 0x0F), 4);
    CHECK_EQ(ui::hamming64(~uint64_t{0}, 0), 64);
}

// --- cluster_similar -------------------------------------------------------

TEST(dup_cluster_groups_within_threshold)
{
    // h[0] and h[1] differ by 1 bit; h[2] is far away; h[3] == h[0].
    const std::vector<uint64_t> hashes{0b0000, 0b0001, ~uint64_t{0}, 0b0000};
    const auto clusters = ui::cluster_similar(hashes, 2);
    REQUIRE(clusters.size() == 1);
    const std::vector<size_t> expect{0, 1, 3};
    CHECK(clusters[0] == expect);
}

TEST(dup_cluster_is_transitive)
{
    // 0-1 within 2 bits, 1-2 within 2 bits, 0-2 is 4 bits: still one cluster.
    const std::vector<uint64_t> hashes{0b0000, 0b0011, 0b1111};
    const auto clusters = ui::cluster_similar(hashes, 2);
    REQUIRE(clusters.size() == 1);
    CHECK_EQ(clusters[0].size(), size_t{3});
}

TEST(dup_cluster_no_singletons_and_empty_input)
{
    const std::vector<uint64_t> lone{0b0000, ~uint64_t{0}};
    CHECK(ui::cluster_similar(lone, 1).empty());
    CHECK(ui::cluster_similar({}, 1).empty());
}
