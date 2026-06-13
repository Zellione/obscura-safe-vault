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
