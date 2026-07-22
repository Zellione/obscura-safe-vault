// Phase 49: pure chip-run layout — how many chips fit in a width, and how many
// are folded into the "+N" counter. Drawing itself is not unit-tested.

#include "test_framework.h"
#include "ui/tag_chip.h"

#include <vector>

TEST(fit_chips_all_fit)
{
    const std::vector<int> w{40, 50, 30};
    const auto f = ui::fit_chips(w, 1000.0f, 30.0f);
    CHECK_EQ(f.shown, 3);
    CHECK_EQ(f.hidden, 0);
}

TEST(fit_chips_drops_and_counts_overflow)
{
    // 40 + spacing + 50 = 102 with CHIP_SPACING 12; a 120 px budget fits two,
    // but only if the "+N" counter also fits — it does not, so one is shown.
    const std::vector<int> w{40, 50, 30};
    const auto f = ui::fit_chips(w, 120.0f, 30.0f);
    CHECK(f.shown >= 1);
    CHECK_EQ(f.shown + f.hidden, 3);
    CHECK(f.hidden > 0);
}

TEST(fit_chips_zero_width_shows_nothing)
{
    const std::vector<int> w{40, 50};
    const auto f = ui::fit_chips(w, 0.0f, 30.0f);
    CHECK_EQ(f.shown, 0);
    CHECK_EQ(f.hidden, 2);
}

TEST(fit_chips_empty_input_is_safe)
{
    const auto f = ui::fit_chips({}, 200.0f, 30.0f);
    CHECK_EQ(f.shown, 0);
    CHECK_EQ(f.hidden, 0);
}

TEST(fit_chips_a_run_that_exactly_fills_the_width_shows_every_chip)
{
    // A run that exactly consumes the available width should show all chips.
    const std::vector<int> w{40, 10};
    const auto f = ui::fit_chips(w, 40.0f + ui::CHIP_SPACING + 10.0f, 30.0f);
    CHECK_EQ(f.shown, 2);
    CHECK_EQ(f.hidden, 0);
}

TEST(lone_chip_text_w_reserves_the_dot_and_gap)
{
    // 100 - (CHIP_DOT 9 + CHIP_GAP 7) = 84, with nothing following.
    CHECK_EQ(ui::lone_chip_text_w(100.0f, 20.0f, 0), 84.0f);
}

TEST(lone_chip_text_w_also_reserves_the_overflow_counter)
{
    // 100 - 16 - (CHIP_SPACING 12 + overflow 20) = 52 when 3 tags follow.
    CHECK_EQ(ui::lone_chip_text_w(100.0f, 20.0f, 3), 52.0f);
}

TEST(lone_chip_text_w_can_leave_no_room_at_all)
{
    // A width this small leaves nothing for text; the caller must handle <= 0.
    CHECK(ui::lone_chip_text_w(10.0f, 20.0f, 1) <= 0.0f);
}
