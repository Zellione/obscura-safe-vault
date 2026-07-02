#include "test_framework.h"
#include "ui/natural_sort.h"

#include <algorithm>
#include <string>
#include <vector>

using ui::natural_compare;
using ui::natural_less;

TEST(natural_sort_numeric_run_beats_lexical)
{
    // The whole point: "2" < "10" numerically (lexically it would be the reverse).
    CHECK(natural_less("2.jpg", "10.jpg"));
    CHECK_FALSE(natural_less("10.jpg", "2.jpg"));
    CHECK(natural_compare("page2.png", "page10.png") < 0);
    CHECK(natural_compare("page10.png", "page2.png") > 0);
}

TEST(natural_sort_leading_zeros_and_equal_values)
{
    // Equal numeric value: fewer leading zeros sorts first.
    CHECK(natural_less("1", "01"));
    CHECK_FALSE(natural_less("01", "1"));
    // Padded numbers still order by value.
    CHECK(natural_less("img008.jpg", "img010.jpg"));
    CHECK(natural_less("img099.jpg", "img100.jpg"));
}

TEST(natural_sort_case_insensitive_letters)
{
    // Letters compare case-insensitively; pure case differences tie at 0.
    CHECK_EQ(natural_compare("Page1.JPG", "page1.jpg"), 0);
    CHECK(natural_less("apple", "Banana"));   // 'a' < 'b' regardless of case
    CHECK(natural_less("Apple", "banana"));
}

TEST(natural_sort_prefix_shorter_first)
{
    CHECK(natural_less("img12", "img12a"));
    CHECK_FALSE(natural_less("img12a", "img12"));
    CHECK_EQ(natural_compare("same", "same"), 0);
}

TEST(natural_sort_full_path_ordering)
{
    // Comic pages spread across chapter folders sort into reading order when the
    // comparator is applied to the full archive path.
    std::vector<std::string> v{
        "chapter10/03.jpg", "chapter2/10.jpg", "chapter2/2.jpg", "chapter1/1.jpg"};
    std::ranges::stable_sort(v, [](const std::string& a, const std::string& b) {
        return natural_less(a, b);
    });
    const std::vector<std::string> want{
        "chapter1/1.jpg", "chapter2/2.jpg", "chapter2/10.jpg", "chapter10/03.jpg"};
    CHECK(v == want);
}

TEST(natural_sort_empty_and_digit_vs_letter)
{
    CHECK_EQ(natural_compare("", ""), 0);
    CHECK(natural_less("", "a"));
    CHECK_FALSE(natural_less("a", ""));
    // A digit's byte is below letters, so "9" < "a" falls out of byte comparison.
    CHECK(natural_less("9", "a"));
}
