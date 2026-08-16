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

// Default marks: the first member of every group is the keeper, every other
// member starts marked REMOVE — one Ctrl+Enter dedups without manual marking.
TEST(dup_review_defaults_keep_first_member_only)
{
    ui::DupReview rev({make_group({100, 200, 300})});
    REQUIRE(rev.groups().size() == 1);
    CHECK(rev.groups()[0].members[0].keep);
    CHECK(!rev.groups()[0].members[1].keep);
    CHECK(!rev.groups()[0].members[2].keep);
    CHECK(rev.any_marked());
    CHECK(rev.can_apply());
    CHECK_EQ(rev.marked_count(), size_t{2});
    CHECK_EQ(rev.marked_bytes(), uint64_t{500});
}

TEST(dup_review_toggle_and_totals)
{
    ui::DupReview rev({make_group({100, 100})});
    CHECK(rev.any_marked());          // default: second copy pre-marked REMOVE
    CHECK_EQ(rev.marked_count(), size_t{1});
    CHECK_EQ(rev.marked_bytes(), uint64_t{100});
    const auto paths = rev.marked_paths();
    REQUIRE(paths.size() == 1);
    CHECK_EQ(paths[0], std::string("g/f1"));
    CHECK(rev.toggle(0, 1));          // back to KEEP
    CHECK(!rev.any_marked());
    CHECK(!rev.can_apply());          // nothing marked -> nothing to apply
    CHECK(rev.toggle(0, 1));          // REMOVE second copy again
    CHECK(rev.can_apply());
}

// Space can never unmark a group's last keeper: the toggle is refused so the
// all-REMOVE state is unreachable through the UI.
TEST(dup_review_toggle_refuses_last_keeper)
{
    ui::DupReview rev({make_group({100, 100})});
    CHECK(!rev.toggle(0, 0));         // members[0] is the only keeper
    CHECK(rev.groups()[0].members[0].keep);
    CHECK(!rev.group_all_removed(0));
    CHECK(rev.toggle(0, 1));          // make members[1] a keeper too
    CHECK(rev.toggle(0, 0));          // now members[0] may be removed
    CHECK(!rev.toggle(0, 1));         // ...which makes members[1] the last keeper
    CHECK(rev.groups()[0].members[1].keep);
}

TEST(dup_review_toggle_out_of_range_is_refused)
{
    ui::DupReview rev({make_group({100, 100})});
    CHECK(!rev.toggle(1, 0));
    CHECK(!rev.toggle(0, 2));
}

// touched() reports USER mark changes only: the ctor's pre-applied defaults
// and refused toggles don't count. Gates the review screen's leave-confirm
// and idle-lock suppression.
TEST(dup_review_touched_tracks_user_changes_only)
{
    ui::DupReview rev({make_group({100, 100})});
    CHECK(!rev.touched());            // defaults alone are not user work
    CHECK(!rev.toggle(0, 0));         // refused (last keeper) -> still untouched
    CHECK(!rev.touched());
    CHECK(rev.toggle(0, 1));
    CHECK(rev.touched());

    ui::DupReview rev2({make_group({100, 100, 100})});
    rev2.keep_only(0, 2);
    CHECK(rev2.touched());
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

// ---- Phase 62: perceptual video duplicates — signature model ----

TEST(duration_close_relative_and_absolute)
{
    // 2% of 100 s = 2 s
    CHECK(ui::duration_close(100'000'000, 101'900'000));
    CHECK(!ui::duration_close(100'000'000, 103'000'000));
    // short clips use the 500 ms absolute floor (2% of 3 s is only 60 ms)
    CHECK(ui::duration_close(3'000'000, 3'400'000));
    CHECK(!ui::duration_close(3'000'000, 3'600'000));
    CHECK(ui::duration_close(0, 0));
}

TEST(video_sig_match_frames_decide_when_available)
{
    ui::VideoSig a;
    ui::VideoSig b;
    a.frame_valid = b.frame_valid = 0b11111;
    for (size_t i = 0; i < 5; ++i) {
        a.frame_hash[i] = 0xF0F0F0F0F0F0F0F0ull;
        b.frame_hash[i] = a.frame_hash[i];
    }
    CHECK(ui::video_sig_match(a, b));
    // 2 of 5 positions far apart -> only 3 matched < DUP_VID_MIN_MATCHED
    b.frame_hash[0] = ~a.frame_hash[0];
    b.frame_hash[1] = ~a.frame_hash[1];
    CHECK(!ui::video_sig_match(a, b));
    // 1 of 5 far apart -> 4 matched, still a match
    b.frame_hash[1] = a.frame_hash[1];
    CHECK(ui::video_sig_match(a, b));
    // frames present and agreeing -> a disagreeing poster must NOT veto
    a.poster_ok = b.poster_ok = true;
    a.poster_hash = 0;
    b.poster_hash = ~0ull;
    b.frame_hash[0] = a.frame_hash[0];
    CHECK(ui::video_sig_match(a, b));
}

TEST(video_sig_match_poster_fallback_without_frames)
{
    ui::VideoSig a;                       // frame_valid == 0 (non-FFmpeg build)
    ui::VideoSig b;
    a.poster_ok = b.poster_ok = true;
    a.poster_hash = 0xFFFF0000FFFF0000ull;
    b.poster_hash = a.poster_hash ^ 0x3FFull;   // 10 bits apart == threshold
    CHECK(ui::video_sig_match(a, b));
    b.poster_hash = a.poster_hash ^ 0x7FFull;   // 11 bits apart
    CHECK(!ui::video_sig_match(a, b));
    b.poster_ok = false;                        // no poster either -> never match
    CHECK(!ui::video_sig_match(a, b));
    // frames on ONE side only (other failed every seek) -> poster fallback
    a.frame_valid = 0b11111;
    b.poster_ok = true;
    b.poster_hash = a.poster_hash;
    CHECK(ui::video_sig_match(a, b));
}

TEST(cluster_video_sigs_groups_transitively)
{
    ui::VideoSig s;
    s.frame_valid = 0b11111;
    for (auto& h : s.frame_hash) h = 0xAAAA5555AAAA5555ull;
    ui::VideoSig near1 = s;
    near1.frame_hash[2] ^= 0x7Full;                             // 7 bits at one position
    ui::VideoSig far1 = s;
    for (auto& h : far1.frame_hash) h = ~h;
    const std::vector<ui::VideoSig> sigs{s, near1, far1};
    const std::vector<uint64_t> dur{60'000'000, 60'500'000, 60'000'000};
    const auto clusters = ui::cluster_video_sigs(sigs, dur);
    REQUIRE(clusters.size() == 1);
    CHECK_EQ(clusters[0].size(), size_t{2});                    // s + near1; far1 excluded
}

TEST(cluster_video_sigs_duration_gate_blocks_lookalikes)
{
    ui::VideoSig s;
    s.frame_valid = 0b11111;
    for (auto& h : s.frame_hash) h = 0x1234123412341234ull;
    const std::vector<ui::VideoSig> sigs{s, s};                 // identical sigs
    const std::vector<uint64_t> dur{60'000'000, 90'000'000};    // durations differ
    CHECK(ui::cluster_video_sigs(sigs, dur).empty());
}

// --- wave window (Phase 64) -------------------------------------------------

namespace {

ui::DupGroup make_group(const std::string& prefix, size_t members, uint64_t bytes = 100)
{
    ui::DupGroup g;
    g.kind = ui::DupGroup::Kind::Identical;
    for (size_t i = 0; i < members; ++i) {
        ui::DupMember m;
        m.node_path = prefix + "/" + std::to_string(i);
        m.name      = std::to_string(i);
        m.bytes     = bytes;
        g.members.push_back(std::move(m));
    }
    return g;
}

// n identical-reclaimable groups: the ctor's stable_sort keeps insertion
// order, so group k's paths keep the "g<k>/" prefix.
std::vector<ui::DupGroup> make_groups(size_t n, size_t members = 2)
{
    std::vector<ui::DupGroup> out;
    for (size_t i = 0; i < n; ++i)
        out.push_back(make_group("g" + std::to_string(i), members));
    return out;
}

} // namespace

TEST(dup_wave_partition_counts)
{
    CHECK_EQ(ui::DupReview{}.wave_count(), size_t{0});
    CHECK_EQ(ui::DupReview{}.wave_size(),  size_t{0});
    CHECK_EQ(ui::DupReview(make_groups(1)).wave_count(),  size_t{1});
    CHECK_EQ(ui::DupReview(make_groups(20)).wave_count(), size_t{1});
    CHECK_EQ(ui::DupReview(make_groups(21)).wave_count(), size_t{2});
    CHECK_EQ(ui::DupReview(make_groups(45)).wave_count(), size_t{3});
    CHECK_EQ(ui::DupReview(make_groups(45)).wave_size(),  size_t{20});
    CHECK_EQ(ui::DupReview(make_groups(45)).wave_index(), size_t{0});
}

TEST(dup_wave_finish_advances_and_erases)
{
    ui::DupReview r(make_groups(45));
    r.finish_wave();
    CHECK_EQ(r.groups().size(), size_t{25});
    CHECK_EQ(r.wave_index(), size_t{1});
    CHECK_EQ(r.wave_count(), size_t{3});
    CHECK_EQ(r.wave_size(),  size_t{20});
    r.finish_wave();
    CHECK_EQ(r.groups().size(), size_t{5});
    CHECK_EQ(r.wave_size(),  size_t{5});
    r.finish_wave();
    CHECK(r.groups().empty());
    CHECK_EQ(r.wave_index(), size_t{3});
    CHECK_EQ(r.wave_count(), size_t{3});
    r.finish_wave();                       // no-op on empty
    CHECK_EQ(r.wave_index(), size_t{3});
}

TEST(dup_wave_scopes_marked_queries)
{
    ui::DupReview r(make_groups(25));      // default marks: 1 REMOVE per group
    CHECK_EQ(r.marked_count(), size_t{20});
    CHECK_EQ(r.marked_bytes(), uint64_t{20 * 100});
    const auto paths = r.marked_paths();
    REQUIRE(paths.size() == 20);
    for (const auto& p : paths)            // nothing from groups 20..24
        CHECK(p != "g20/1" && p != "g21/1" && p != "g22/1" && p != "g23/1" && p != "g24/1");
    r.finish_wave();
    CHECK_EQ(r.marked_count(), size_t{5});
}

TEST(dup_wave_touched_resets_on_finish)
{
    ui::DupReview r(make_groups(21));
    CHECK(!r.touched());
    CHECK(r.toggle(0, 1));                 // REMOVE -> KEEP, user-touched
    CHECK(r.touched());
    r.finish_wave();
    CHECK(!r.touched());
}

TEST(dup_wave_marks_beyond_wave_are_unreachable)
{
    ui::DupReview r(make_groups(21));
    CHECK(!r.toggle(20, 1));               // group 20 is in wave 2
    r.keep_only(20, 1);                    // must be a no-op
    CHECK(!r.touched());
    CHECK(!r.groups()[20].members[1].keep);   // still the pre-applied default
}

TEST(dup_wave_can_apply_goes_false_when_empty)
{
    ui::DupReview r(make_groups(25));
    CHECK(r.can_apply());
    r.finish_wave();
    r.finish_wave();
    CHECK(!r.any_marked());
    CHECK(!r.can_apply());
}

TEST(dup_refresh_drops_vanished_and_shrunken)
{
    std::vector<ui::DupGroup> gs{make_group("a", 3), make_group("b", 2)};
    ui::DupReview r(std::move(gs));
    r.refresh_members([](ui::DupMember& m) { return m.node_path != "b/0"; });
    REQUIRE(r.groups().size() == 1);           // b fell below 2 members
    CHECK_EQ(r.groups()[0].members.size(), size_t{3});
    CHECK_EQ(r.groups()[0].members[0].node_path, std::string("a/0"));
}

TEST(dup_refresh_applies_field_updates)
{
    ui::DupReview r(std::vector<ui::DupGroup>{make_group("a", 2)});
    r.refresh_members([](ui::DupMember& m) {
        m.thumb_offset = 777;
        m.data_spans   = {{123, 456}};
        return true;
    });
    CHECK_EQ(r.groups()[0].members[0].thumb_offset, uint64_t{777});
    CHECK_EQ(r.groups()[0].members[1].data_spans[0].first, uint64_t{123});
}

TEST(dup_refresh_reapplies_default_marks)
{
    ui::DupReview r(std::vector<ui::DupGroup>{make_group("a", 3)});
    // Default marks: a/0 KEEP, a/1 + a/2 REMOVE. Drop the keeper: the new
    // first member must become the keeper.
    r.refresh_members([](ui::DupMember& m) { return m.node_path != "a/0"; });
    REQUIRE(r.groups().size() == 1);
    REQUIRE(r.groups()[0].members.size() == 2);
    CHECK(r.groups()[0].members[0].keep);      // a/1, re-defaulted to KEEP
    CHECK(!r.groups()[0].members[1].keep);     // a/2
}

// --- Phase 86: gallery-vs-vault scope filter --------------------------------

static ui::DupGroup group_with_paths(std::vector<std::string> paths)
{
    ui::DupGroup g;
    for (auto& p : paths) {
        ui::DupMember m;
        m.node_path = std::move(p);
        g.members.push_back(std::move(m));
    }
    return g;
}

TEST(dup_scope_filter_keeps_groups_touching_the_gallery)
{
    // One member inside the scope is enough — and the OUTSIDE members stay in
    // the group, so the review can remove either side of the duplication.
    std::vector<ui::DupGroup> groups{
        group_with_paths({"a/one.png", "elsewhere/two.png"})};
    const size_t dropped = ui::scope_filter_groups(groups, "a");
    CHECK_EQ(dropped, size_t{0});
    REQUIRE(groups.size() == 1);
    CHECK_EQ(groups[0].members.size(), size_t{2});
}

TEST(dup_scope_filter_drops_groups_outside_the_gallery)
{
    std::vector<ui::DupGroup> groups{
        group_with_paths({"a/one.png", "elsewhere/two.png"}),
        group_with_paths({"x/p.png", "y/q.png"})};
    const size_t dropped = ui::scope_filter_groups(groups, "a");
    CHECK_EQ(dropped, size_t{1});
    REQUIRE(groups.size() == 1);
    CHECK_EQ(groups[0].members[0].node_path, std::string("a/one.png"));
}

TEST(dup_scope_filter_respects_component_boundaries)
{
    // "ab/x.png" is NOT inside gallery "a" — a bare prefix match would leak
    // sibling galleries that merely share a name prefix.
    std::vector<ui::DupGroup> groups{
        group_with_paths({"ab/x.png", "ab/y.png"})};
    const size_t dropped = ui::scope_filter_groups(groups, "a");
    CHECK_EQ(dropped, size_t{1});
    CHECK(groups.empty());
}

TEST(dup_scope_filter_matches_nested_descendants)
{
    std::vector<ui::DupGroup> groups{
        group_with_paths({"a/b/deep.png", "other/copy.png"})};
    const size_t dropped = ui::scope_filter_groups(groups, "a");
    CHECK_EQ(dropped, size_t{0});
    CHECK_EQ(groups.size(), size_t{1});
}

TEST(dup_scope_filter_empty_scope_keeps_everything)
{
    std::vector<ui::DupGroup> groups{
        group_with_paths({"x/p.png", "y/q.png"})};
    const size_t dropped = ui::scope_filter_groups(groups, "");
    CHECK_EQ(dropped, size_t{0});
    CHECK_EQ(groups.size(), size_t{1});
}

// --- Phase 86: scan-scope labels --------------------------------------------

TEST(dup_scope_label_whole_vault)
{
    CHECK_EQ(ui::dup_scope_label(ui::DupScope::WholeVault, "a/b"),
             std::string("Whole vault"));
}

TEST(dup_scope_label_gallery_only_names_the_gallery)
{
    CHECK_EQ(ui::dup_scope_label(ui::DupScope::GalleryOnly, "a/b"),
             std::string("This gallery only — a/b"));
}

TEST(dup_scope_label_gallery_vs_vault_names_the_gallery)
{
    CHECK_EQ(ui::dup_scope_label(ui::DupScope::GalleryVsVault, "a/b"),
             std::string("This gallery vs whole vault — a/b"));
}
