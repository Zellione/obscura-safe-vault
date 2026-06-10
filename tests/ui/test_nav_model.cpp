#include "test_framework.h"

#include "ui/nav_model.h"

TEST(nav_split_join_roundtrip)
{
    auto segs = ui::split_path("alpha/beta/gamma");
    REQUIRE(segs.size() == 3);
    CHECK(segs[0] == "alpha");
    CHECK(segs[2] == "gamma");
    CHECK(ui::join_path(segs) == "alpha/beta/gamma");
    CHECK(ui::split_path("").empty());
    CHECK(ui::join_path({}) == "");
}

TEST(nav_enter_up_path)
{
    ui::NavModel m;
    CHECK(m.path() == "");
    CHECK_FALSE(m.up());            // already at root
    m.enter("photos");
    m.enter("2024");
    CHECK(m.path() == "photos/2024");
    REQUIRE(m.up());
    CHECK(m.path() == "photos");
}

TEST(nav_selection_clamp)
{
    ui::NavModel m;
    m.set_count(3);
    CHECK_EQ(m.selected(), 0);
    m.move(-1);
    CHECK_EQ(m.selected(), 0);     // clamp low
    m.move(5);
    CHECK_EQ(m.selected(), 2);     // clamp high
    m.select(1);
    CHECK_EQ(m.selected(), 1);
    m.set_count(0);
    CHECK_EQ(m.selected(), 0);     // empty resets
}
