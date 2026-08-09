#include "test_framework.h"
#include "ui/position_label.h"

// Pure position_label formatter (Phase 68 Part 4, Task A).
// Tests: formats 0-based index to 1-based position "N / Count" string,
// empty string for out-of-range or empty.

namespace {

TEST(position_label_formats_one_based)
{
    CHECK_EQ(ui::position_label(0, 5), "1 / 5");
    CHECK_EQ(ui::position_label(4, 5), "5 / 5");
    CHECK_EQ(ui::position_label(0, 1), "1 / 1");
}

TEST(position_label_empty_on_empty_or_out_of_range)
{
    CHECK_EQ(ui::position_label(0, 0), "");
    CHECK_EQ(ui::position_label(-1, 5), "");
    CHECK_EQ(ui::position_label(5, 5), "");
    CHECK_EQ(ui::position_label(100, 50), "");
}

TEST(position_label_formats_large_counts)
{
    CHECK_EQ(ui::position_label(0, 1000), "1 / 1000");
    CHECK_EQ(ui::position_label(999, 1000), "1000 / 1000");
    CHECK_EQ(ui::position_label(500, 1000), "501 / 1000");
}

} // namespace
