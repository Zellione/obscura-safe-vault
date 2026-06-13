#include "test_framework.h"

#include <functional>
#include <string>

#include "ui/widgets.h"

namespace {
// Fake measure: every character is 10px wide (so "..." is 30px).
const std::function<int(std::string_view)> w10 =
    [](std::string_view s) { return static_cast<int>(s.size()) * 10; };
}

TEST(elide_middle_keeps_names_that_fit)
{
    CHECK_EQ(ui::elide_middle("abc", 100, w10), std::string("abc"));
    CHECK_EQ(ui::elide_middle("abcdefghij", 100, w10), std::string("abcdefghij")); // exactly 100
}

TEST(elide_middle_cuts_in_the_middle)
{
    // 11 chars (110px) into 100px -> "abcd" + "..." + "ijk" = 10 chars = 100px.
    CHECK_EQ(ui::elide_middle("abcdefghijk", 100, w10), std::string("abcd...ijk"));
}

TEST(elide_middle_returns_empty_when_even_ellipsis_wont_fit)
{
    CHECK_EQ(ui::elide_middle("abcdefghijk", 20, w10), std::string());
}
