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

// --- DupReview -------------------------------------------------------------

static ui::DupGroup make_group(std::initializer_list<uint64_t> sizes)
{
    ui::DupGroup g;
    int i = 0;
    for (uint64_t b : sizes) {
        ui::DupMember m;
        m.name      = "f" + std::to_string(i);
        m.node_path = "g/f" + std::to_string(i++);
        m.bytes     = b;
        g.members.push_back(std::move(m));
    }
    return g;
}

TEST(dup_group_reclaimable_is_all_but_largest)
{
    CHECK_EQ(ui::group_reclaimable(make_group({100, 300, 200})), uint64_t{300});
    CHECK_EQ(ui::group_reclaimable(make_group({50})), uint64_t{0});
}

TEST(dup_review_sorts_groups_by_reclaimable_desc)
{
    std::vector<ui::DupGroup> gs;
    gs.push_back(make_group({10, 10}));        // reclaimable 10
    gs.push_back(make_group({500, 500, 500})); // reclaimable 1000
    const ui::DupReview rev(std::move(gs));
    REQUIRE(rev.groups().size() == 2);
    CHECK_EQ(rev.groups()[0].members[0].bytes, uint64_t{500});
}

TEST(dup_review_toggle_and_totals)
{
    ui::DupReview rev({make_group({100, 100})});
    CHECK(!rev.any_marked());
    CHECK(!rev.can_apply());          // nothing marked -> nothing to apply
    rev.toggle(0, 1);                 // REMOVE second copy
    CHECK(rev.any_marked());
    CHECK(rev.can_apply());
    CHECK_EQ(rev.marked_count(), size_t{1});
    CHECK_EQ(rev.marked_bytes(), uint64_t{100});
    const auto paths = rev.marked_paths();
    REQUIRE(paths.size() == 1);
    CHECK_EQ(paths[0], std::string("g/f1"));
    rev.toggle(0, 1);                 // back to KEEP
    CHECK(!rev.any_marked());
}

TEST(dup_review_refuses_fully_removed_group)
{
    ui::DupReview rev({make_group({100, 100})});
    rev.toggle(0, 0);
    rev.toggle(0, 1);
    CHECK(rev.group_all_removed(0));
    CHECK(!rev.can_apply());          // would delete every copy
}

TEST(dup_review_keep_only)
{
    ui::DupReview rev({make_group({100, 100, 100})});
    rev.keep_only(0, 2);
    CHECK_EQ(rev.marked_count(), size_t{2});
    CHECK(rev.groups()[0].members[2].keep);
    CHECK(!rev.groups()[0].members[0].keep);
    CHECK(rev.can_apply());
}
