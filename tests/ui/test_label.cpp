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

// Tail elision (Phase 56 follow-up): used where the START of the string carries
// the meaning — a help-popup line is "[key]  description", so keeping the head
// and dropping the tail reads better than cutting the middle out of it.

TEST(elide_tail_keeps_names_that_fit)
{
    CHECK_EQ(ui::elide_tail("abc", 100, w10), std::string("abc"));
    CHECK_EQ(ui::elide_tail("abcdefghij", 100, w10), std::string("abcdefghij")); // exactly 100
}

TEST(elide_tail_cuts_at_the_end)
{
    // 11 chars (110px) into 100px -> "abcdefg" + "..." = 10 chars = 100px.
    CHECK_EQ(ui::elide_tail("abcdefghijk", 100, w10), std::string("abcdefg..."));
}

TEST(elide_tail_keeps_the_head_the_middle_form_would_have_split)
{
    // The distinguishing property: everything before the ellipsis is a prefix of
    // the input, which is what makes a "[key]  description" line still readable.
    const std::string out = ui::elide_tail("[Ctrl+C] Copy to clipboard", 100, w10);
    CHECK(out.ends_with("..."));
    CHECK(std::string_view("[Ctrl+C] Copy to clipboard").starts_with(
              std::string_view(out).substr(0, out.size() - 3)));
}

TEST(elide_tail_returns_empty_when_even_ellipsis_wont_fit)
{
    CHECK_EQ(ui::elide_tail("abcdefghijk", 20, w10), std::string());
}
