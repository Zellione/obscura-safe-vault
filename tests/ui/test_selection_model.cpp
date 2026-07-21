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
