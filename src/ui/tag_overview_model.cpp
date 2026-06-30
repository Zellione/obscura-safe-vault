#include "ui/tag_overview_model.h"

#include <algorithm>
#include <cctype>

namespace ui {

namespace {

// Case-insensitive ASCII less-than over two strings (lexicographic).
bool ci_less(std::string_view a, std::string_view b) noexcept
{
    return std::ranges::lexicographical_compare(a, b, [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) <
               std::tolower(static_cast<unsigned char>(y));
    });
}

// Whether `s` begins with `prefix`, comparing ASCII case-insensitively.
bool ci_starts_with(std::string_view s, std::string_view prefix) noexcept
{
    if (prefix.size() > s.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(s[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    return true;
}

} // namespace

void sort_tags(std::vector<TagTally>& tags, TagSort sort)
{
    if (sort == TagSort::Name) {
        std::ranges::sort(tags, [](const TagTally& a, const TagTally& b) {
            return ci_less(a.tag, b.tag);
        });
        return;
    }
    // Count: descending total, ties broken by case-insensitive name ascending.
    std::ranges::sort(tags, [](const TagTally& a, const TagTally& b) {
        if (a.total() != b.total()) return a.total() > b.total();
        return ci_less(a.tag, b.tag);
    });
}

std::vector<TagTally> filter_tags(const std::vector<TagTally>& tags, std::string_view prefix)
{
    if (prefix.empty()) return tags;
    std::vector<TagTally> out;
    for (const auto& t : tags)
        if (ci_starts_with(t.tag, prefix)) out.push_back(t);
    return out;
}

} // namespace ui
