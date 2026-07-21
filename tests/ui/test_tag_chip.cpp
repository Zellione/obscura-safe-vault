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

TEST(fit_chips_never_hides_exactly_one_behind_a_wider_counter)
{
    // If the last chip is narrower than the "+1" counter, showing it is both
    // cheaper and more useful than counting it.
    const std::vector<int> w{40, 10};
    const auto f = ui::fit_chips(w, 40.0f + ui::CHIP_SPACING + 10.0f, 30.0f);
    CHECK_EQ(f.shown, 2);
    CHECK_EQ(f.hidden, 0);
}
