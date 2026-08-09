#include "test_framework.h"

#include "ui/parent_group.h"

#include <string>
#include <vector>

// Phase 68: collection screens (favorites, tags, search results) list items
// from DIFFERENT parent galleries; export and transfer group them per parent.

TEST(group_by_parent_splits_and_preserves_order)
{
    const std::vector<std::string> paths{
        "trip/a.jpg", "trip/b.jpg", "other/c.jpg", "trip/d.jpg",
    };
    const auto groups = ui::group_by_parent(paths);
    REQUIRE(groups.size() == static_cast<size_t>(2));
    CHECK_EQ(groups[0].parent, std::string("trip"));
    REQUIRE(groups[0].names.size() == static_cast<size_t>(3));
    CHECK_EQ(groups[0].names[0], std::string("a.jpg"));
    CHECK_EQ(groups[0].names[1], std::string("b.jpg"));
    CHECK_EQ(groups[0].names[2], std::string("d.jpg"));
    CHECK_EQ(groups[1].parent, std::string("other"));
    REQUIRE(groups[1].names.size() == static_cast<size_t>(1));
    CHECK_EQ(groups[1].names[0], std::string("c.jpg"));
}

TEST(group_by_parent_root_items_use_empty_parent)
{
    const std::vector<std::string> paths{"solo.jpg", "deep/nest/x.jpg"};
    const auto groups = ui::group_by_parent(paths);
    REQUIRE(groups.size() == static_cast<size_t>(2));
    CHECK_EQ(groups[0].parent, std::string(""));
    CHECK_EQ(groups[0].names[0], std::string("solo.jpg"));
    CHECK_EQ(groups[1].parent, std::string("deep/nest"));
    CHECK_EQ(groups[1].names[0], std::string("x.jpg"));
}

TEST(group_by_parent_empty_input_is_empty)
{
    CHECK(ui::group_by_parent({}).empty());
}
