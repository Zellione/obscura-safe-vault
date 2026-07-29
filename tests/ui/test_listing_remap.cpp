#include "ui/listing_remap.h"

#include <string>
#include <vector>

#include "test_framework.h"

using ui::remap_listing;

static std::vector<std::string> names(std::initializer_list<const char*> l)
{ return {l.begin(), l.end()}; }

TEST(remap_identical_listing_is_unchanged)
{
    const auto n = names({"a", "b", "c"});
    const auto m = remap_listing(n, n, 1, std::vector<int>{0, 2});
    CHECK(m.unchanged);
    CHECK_EQ(m.selected, 1);
    REQUIRE(m.multi.size() == 2u);
    CHECK_EQ(m.multi[0], 0); CHECK_EQ(m.multi[1], 2);
}

TEST(remap_prepended_items_shift_selection_and_multi)
{
    // Import prepended two new items (e.g. DateDesc sort) — the user's focus
    // must stay on "b", not jump to whatever now sits at index 1.
    const auto m = remap_listing(names({"a", "b", "c"}),
                                 names({"x", "y", "a", "b", "c"}),
                                 1, std::vector<int>{2});
    CHECK(!m.unchanged);
    CHECK_EQ(m.selected, 3);                       // "b"
    REQUIRE(m.multi.size() == 1u); CHECK_EQ(m.multi[0], 4);   // "c"
}

TEST(remap_appended_items_keep_selection)
{
    const auto m = remap_listing(names({"a", "b"}), names({"a", "b", "c"}),
                                 0, std::vector<int>{});
    CHECK_EQ(m.selected, 0);
    CHECK(!m.unchanged);
}

TEST(remap_vanished_selected_name_clamps)
{
    const auto m = remap_listing(names({"a", "b", "c"}), names({"a", "c"}),
                                 1, std::vector<int>{1});
    CHECK_EQ(m.selected, 1);          // clamp(1, 0, 1)
    CHECK(m.multi.empty());           // "b" is gone from the multi-selection
}

TEST(remap_to_empty_listing)
{
    const auto m = remap_listing(names({"a"}), {}, 0, std::vector<int>{0});
    CHECK_EQ(m.selected, 0);
    CHECK(m.multi.empty());
    CHECK(!m.unchanged);
}

TEST(remap_out_of_range_old_selected_clamps)
{
    const auto m = remap_listing({}, names({"a", "b"}), 5, std::vector<int>{});
    CHECK_EQ(m.selected, 1);
}
