#include "test_framework.h"

#include "ui/selection_model.h"

TEST(selection_starts_empty)
{
    ui::SelectionModel s;
    CHECK(s.empty());
    CHECK_EQ(s.count(), 0u);
    CHECK_FALSE(s.contains(0));
    CHECK(s.indices().empty());
}

TEST(selection_toggle_adds_and_removes)
{
    ui::SelectionModel s;
    s.toggle(2);
    CHECK(s.contains(2));
    CHECK_EQ(s.count(), 1u);
    CHECK_FALSE(s.empty());

    s.toggle(2);                 // toggling again removes it
    CHECK_FALSE(s.contains(2));
    CHECK(s.empty());
}

TEST(selection_supports_multiple_and_sorted_indices)
{
    ui::SelectionModel s;
    s.toggle(5);
    s.toggle(1);
    s.toggle(3);
    CHECK_EQ(s.count(), 3u);

    auto idx = s.indices();      // ascending order regardless of insertion
    REQUIRE(idx.size() == 3);
    CHECK_EQ(idx[0], 1);
    CHECK_EQ(idx[1], 3);
    CHECK_EQ(idx[2], 5);
}

TEST(selection_clear_empties_everything)
{
    ui::SelectionModel s;
    s.toggle(0);
    s.toggle(7);
    s.clear();
    CHECK(s.empty());
    CHECK_EQ(s.count(), 0u);
    CHECK_FALSE(s.contains(7));
}

TEST(selection_revision_advances_on_toggle)
{
    ui::SelectionModel s;
    const uint64_t r0 = s.revision();
    s.toggle(3);
    const uint64_t r1 = s.revision();
    CHECK(r1 != r0);
    s.toggle(3);                 // deselecting is also a change
    CHECK(s.revision() != r1);
}

TEST(selection_revision_distinguishes_same_size_selections)
{
    // The whole reason the counter exists: {1} and {2} are both size 1.
    ui::SelectionModel s;
    s.toggle(1);
    const uint64_t r1 = s.revision();
    s.toggle(1);
    s.toggle(2);
    CHECK_EQ(s.count(), static_cast<std::size_t>(1));
    CHECK(s.revision() != r1);
}

TEST(selection_revision_advances_on_clear)
{
    ui::SelectionModel s;
    s.toggle(1);
    const uint64_t r1 = s.revision();
    s.clear();
    CHECK(s.revision() != r1);
}

TEST(selection_revision_is_stable_across_reads)
{
    ui::SelectionModel s;
    s.toggle(1);
    const uint64_t r1 = s.revision();
    (void)s.contains(1);
    (void)s.indices();
    (void)s.empty();
    CHECK_EQ(s.revision(), r1);
}

// --- Phase 53: Ctrl+A select-all -------------------------------------------

TEST(selection_select_all_selects_every_index)
{
    ui::SelectionModel s;
    s.select_all(4);
    CHECK_EQ(s.count(), static_cast<std::size_t>(4));
    CHECK(s.contains(0));
    CHECK(s.contains(3));
    CHECK_FALSE(s.contains(4));
}

TEST(selection_select_all_is_idempotent)
{
    ui::SelectionModel s;
    s.select_all(3);
    s.select_all(3);
    CHECK_EQ(s.count(), static_cast<std::size_t>(3));
}

TEST(selection_select_all_absorbs_a_partial_selection)
{
    // Selecting all after picking a couple by hand must not double-count or
    // leave gaps.
    ui::SelectionModel s;
    s.toggle(1);
    s.select_all(3);
    CHECK_EQ(s.count(), static_cast<std::size_t>(3));
}

TEST(selection_select_all_bumps_revision)
{
    ui::SelectionModel s;
    const uint64_t before = s.revision();
    s.select_all(2);
    CHECK(s.revision() != before);
}

TEST(selection_select_all_of_empty_listing_does_nothing)
{
    // "Select all of nothing" is not a request to deselect: an empty gallery
    // must not silently wipe a selection carried in from elsewhere.
    ui::SelectionModel s;
    s.toggle(7);
    s.select_all(0);
    CHECK_EQ(s.count(), static_cast<std::size_t>(1));
    s.select_all(-1);
    CHECK_EQ(s.count(), static_cast<std::size_t>(1));
}

TEST(selection_all_selected_reports_full_listing)
{
    ui::SelectionModel s;
    s.select_all(3);
    CHECK(s.all_selected(3));
}

TEST(selection_all_selected_false_when_partial)
{
    ui::SelectionModel s;
    s.toggle(0);
    s.toggle(1);
    CHECK_FALSE(s.all_selected(3));
}

TEST(selection_all_selected_false_for_empty_listing)
{
    // Otherwise Ctrl+A on an empty gallery would report "already all selected"
    // and take the clearing branch forever.
    ui::SelectionModel s;
    CHECK_FALSE(s.all_selected(0));
}

TEST(selection_all_selected_ignores_stale_out_of_range_indices)
{
    // A selection can outlive a shrinking listing; indices beyond the listing
    // must not make a partial selection look complete.
    ui::SelectionModel s;
    s.toggle(5);
    s.toggle(6);
    CHECK_FALSE(s.all_selected(2));
}

// --- Phase 86: Shift+Space range selection ----------------------------------

TEST(selection_range_for_without_anchor_is_empty)
{
    ui::SelectionModel s;
    CHECK_FALSE(s.range_for(4).has_value());
}

TEST(selection_range_for_extends_from_last_toggled_anchor)
{
    // Select A with Space, move focus to B, Shift+Space: span A..B.
    ui::SelectionModel s;
    s.toggle(2);
    const auto r = s.range_for(5);
    REQUIRE(r.has_value());
    CHECK_EQ(r->first, 2);
    CHECK_EQ(r->second, 5);
}

TEST(selection_range_for_fills_between_two_recent_selections)
{
    // The literal flow: select A, select B, press the range key while still on
    // B — the span is between the two most recently selected items.
    ui::SelectionModel s;
    s.toggle(2);
    s.toggle(6);
    const auto r = s.range_for(6);
    REQUIRE(r.has_value());
    CHECK_EQ(r->first, 2);
    CHECK_EQ(r->second, 6);
}

TEST(selection_range_for_lone_anchor_at_focus_is_empty)
{
    // One selected item and the focus sitting on it: there is nothing to span.
    ui::SelectionModel s;
    s.toggle(3);
    CHECK_FALSE(s.range_for(3).has_value());
}

TEST(selection_range_for_deselected_anchor_falls_back_to_previous)
{
    // Deselecting the newest anchor hands the role back to the one before it.
    ui::SelectionModel s;
    s.toggle(2);
    s.toggle(6);
    s.toggle(6);                 // deselect the newest anchor
    const auto r = s.range_for(4);
    REQUIRE(r.has_value());
    CHECK_EQ(r->first, 2);
    CHECK_EQ(r->second, 4);
}

TEST(selection_range_for_deselected_only_anchor_is_empty)
{
    ui::SelectionModel s;
    s.toggle(2);
    s.toggle(2);
    CHECK_FALSE(s.range_for(5).has_value());
}

TEST(selection_range_for_survives_a_range_fill)
{
    // A fill does not move the anchor: extending again re-spans from the same
    // item, file-manager style.
    ui::SelectionModel s;
    s.toggle(2);
    s.select_range(2, 5);
    const auto r = s.range_for(7);
    REQUIRE(r.has_value());
    CHECK_EQ(r->first, 2);
    CHECK_EQ(r->second, 7);
}

TEST(selection_clear_resets_the_anchor)
{
    ui::SelectionModel s;
    s.toggle(2);
    s.clear();
    CHECK_FALSE(s.range_for(5).has_value());
}

TEST(selection_select_all_does_not_establish_an_anchor)
{
    // Ctrl+A is not a hand-picked selection; a later Shift+Space must not
    // span from some arbitrary index.
    ui::SelectionModel s;
    s.select_all(4);
    CHECK_FALSE(s.range_for(2).has_value());
}

TEST(selection_select_range_selects_inclusive_span_both_orders)
{
    ui::SelectionModel s;
    s.select_range(5, 2);        // descending order works too
    CHECK_EQ(s.count(), static_cast<std::size_t>(4));
    CHECK(s.contains(2));
    CHECK(s.contains(3));
    CHECK(s.contains(4));
    CHECK(s.contains(5));
    CHECK_FALSE(s.contains(1));
    CHECK_FALSE(s.contains(6));
}

TEST(selection_select_adds_without_establishing_an_anchor)
{
    // The gallery grid's filtered range fill selects item by item; the fill
    // must not move the anchor the way a hand toggle does.
    ui::SelectionModel s;
    s.toggle(2);
    s.select(9);
    s.select(9);                 // idempotent, never deselects
    CHECK(s.contains(9));
    const auto r = s.range_for(5);
    REQUIRE(r.has_value());
    CHECK_EQ(r->first, 2);
}

TEST(selection_select_range_bumps_revision_once)
{
    ui::SelectionModel s;
    const uint64_t before = s.revision();
    s.select_range(1, 3);
    CHECK_EQ(s.revision(), before + 1);
}
