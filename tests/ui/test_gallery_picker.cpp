#include "test_framework.h"

#include "ui/gallery_picker.h"

TEST(gallery_picker_starts_unfiltered_with_all_items)
{
    ui::GalleryPickerModel m;
    m.set_items({"A", "B", "C"});
    CHECK(m.filtered().size() == 3);
    CHECK_FALSE(m.filter_open());
    CHECK(m.selected() == 0);
}

TEST(gallery_picker_filter_narrows_by_substring)
{
    ui::GalleryPickerModel m;
    m.set_items({"Trips/2024", "Trips/2025", "Family/Reunion"});
    m.open_filter();
    CHECK(m.filter_open());
    m.filter_append("trip");
    REQUIRE(m.filtered().size() == 2);
    CHECK(m.filtered()[0] == "Trips/2024");
    CHECK(m.filtered()[1] == "Trips/2025");
}

TEST(gallery_picker_filter_backspace_and_clear)
{
    ui::GalleryPickerModel m;
    m.set_items({"Trips", "Family"});
    m.open_filter();
    m.filter_append("family");
    REQUIRE(m.filtered().size() == 1);
    m.filter_backspace();   // "famil"
    CHECK(m.filter() == "famil");
    CHECK(m.filtered().size() == 1);   // still a substring match
    m.filter_clear();
    CHECK(m.filter().empty());
    CHECK(m.filtered().size() == 2);
}

TEST(gallery_picker_close_filter_keeps_items_but_stops_typing)
{
    ui::GalleryPickerModel m;
    m.set_items({"Trips", "Family"});
    m.open_filter();
    m.filter_append("trips");
    m.close_filter();
    CHECK_FALSE(m.filter_open());
    CHECK(m.filtered().size() == 1);   // filter text (and its effect) survives closing
}

TEST(gallery_picker_set_items_resets_filter_and_selection)
{
    ui::GalleryPickerModel m;
    m.set_items({"A", "B"});
    m.open_filter();
    m.filter_append("a");
    m.move(0);
    m.set_items({"X", "Y", "Z"});
    CHECK_FALSE(m.filter_open());
    CHECK(m.filter().empty());
    CHECK(m.filtered().size() == 3);
    CHECK(m.selected() == 0);
}

TEST(gallery_picker_move_clamps_to_filtered_range)
{
    ui::GalleryPickerModel m;
    m.set_items({"A", "B", "C"});
    m.move(-5);
    CHECK(m.selected() == 0);
    m.move(5);
    CHECK(m.selected() == 2);
    m.move(-1);
    CHECK(m.selected() == 1);
}

TEST(gallery_picker_move_on_empty_filtered_list_stays_zero)
{
    ui::GalleryPickerModel m;
    m.set_items({"Trips"});
    m.open_filter();
    m.filter_append("zzz");   // matches nothing
    CHECK(m.filtered().empty());
    m.move(1);
    CHECK(m.selected() == 0);
}

TEST(gallery_picker_geom_no_scroll_when_everything_fits)
{
    ui::GalleryPickerModel m;
    m.set_items({"A", "B", "C"});
    const auto g = m.geom(10);
    CHECK(g.first == 0);
    CHECK(g.visible == 3);
}

TEST(gallery_picker_geom_centers_selection_when_scrolled)
{
    ui::GalleryPickerModel m;
    std::vector<std::string> items;
    for (int i = 0; i < 20; ++i) items.push_back("item" + std::to_string(i));
    m.set_items(items);
    m.move(15);              // selected() == 15
    const auto g = m.geom(5);   // 5 rows visible out of 20
    CHECK(g.visible == 5);
    CHECK(g.first <= 15);
    CHECK(g.first + g.visible > 15);        // selection stays on-screen
    CHECK(g.first <= 20 - g.visible);       // never scrolls past the end
}

TEST(gallery_picker_geom_at_start_does_not_scroll_negative)
{
    ui::GalleryPickerModel m;
    std::vector<std::string> items;
    for (int i = 0; i < 20; ++i) items.push_back("item" + std::to_string(i));
    m.set_items(items);
    const auto g = m.geom(5);
    CHECK(g.first == 0);
}

TEST(gallery_picker_pinned_suffix_survives_non_matching_filter)
{
    ui::GalleryPickerModel m;
    m.set_items({"Trips", "Family"});
    m.set_pinned_suffix("+ New gallery…");
    m.open_filter();
    m.filter_append("zzz-nomatch");
    REQUIRE(m.filtered().size() == 1);
    CHECK(m.filtered()[0] == "+ New gallery…");
}

TEST(gallery_picker_pinned_suffix_not_duplicated_when_it_also_matches)
{
    ui::GalleryPickerModel m;
    m.set_items({"Trips", "Family"});
    m.set_pinned_suffix("+ New gallery…");
    m.open_filter();
    m.filter_append("new");   // "new" is a substring of "+ New gallery…" case-insensitively
    REQUIRE(m.filtered().size() == 1);
    CHECK(m.filtered()[0] == "+ New gallery…");
}

TEST(picker_multi_select_toggle_and_checked_order)
{
    ui::GalleryPickerModel m;
    m.set_items({"a", "a/b", "c"});
    m.set_multi(true);
    m.toggle_checked();               // row 0 = "a"
    m.move(2);  m.toggle_checked();   // row 2 = "c"
    CHECK(m.is_checked("a"));
    CHECK(!m.is_checked("a/b"));
    CHECK(m.checked() == std::vector<std::string>({"a", "c"}));
    m.toggle_checked();               // untoggle "c"
    CHECK(m.checked() == std::vector<std::string>({"a"}));
}

TEST(picker_multi_select_respects_filter_and_pinned)
{
    ui::GalleryPickerModel m;
    m.set_items({"a", "a/b", "c"});
    m.set_multi(true);
    m.set_pinned_suffix("+ New");
    m.filter_append("a");
    REQUIRE(m.filtered().size() == 3);  // "a", "a/b", "+ New"
    m.toggle_checked();  // checks "a"
    m.move(2);  // move to pinned suffix
    m.toggle_checked();  // should be no-op on pinned row
    CHECK(m.checked() == std::vector<std::string>({"a"}));  // only "a" checked

    // set_items() clears multi + checks
    m.set_items({"x", "y"});
    CHECK(!m.multi());
    CHECK(m.checked().empty());
}

TEST(drop_descendant_paths_prunes_subtrees)
{
    auto out = ui::drop_descendant_paths({"a/b", "a", "c/d", "ab"});
    CHECK(out == std::vector<std::string>({"a", "ab", "c/d"}));   // sorted order
}
