#include "ui/tag_suggest.h"

#include <algorithm>

#include "ui/advanced_search_model.h"
#include "ui/tag_inherit.h"

namespace ui {

namespace {

std::string_view trimmed(std::string_view s)
{
    const auto first = s.find_first_not_of(" \t\n\r");
    if (first == std::string_view::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\n\r") - first + 1);
}

} // namespace

std::vector<std::string> editor_tag_suggestions(std::string_view                buffer,
                                                const std::vector<std::string>& vocabulary,
                                                const std::vector<std::string>& own_tags)
{
    const std::string_view typed = trimmed(buffer);
    if (typed.empty()) return {};

    std::vector<std::string> out = tag_suggestions(typed, vocabulary);
    std::erase_if(out, [&own_tags](const std::string& s) {
        return std::ranges::any_of(
            own_tags, [&s](const std::string& own) { return tag_ci_equal(own, s); });
    });
    if (out.size() > static_cast<size_t>(TAG_SUGGEST_MAX))
        out.resize(static_cast<size_t>(TAG_SUGGEST_MAX));
    return out;
}

std::vector<std::string> field_value_suggestions(std::string_view             buffer,
                                                 std::string_view             category,
                                                 std::string_view             field,
                                                 const vault::VaultSettings&  settings)
{
    std::vector<std::string> pool;
    for (const auto& e : settings.tag_field_values) {
        if (!tag_ci_equal(e.field.view(), field)) continue;
        if (!tag_ci_equal(vault::tag_category_prefix(e.tag.view()), category)) continue;
        const bool dupe = std::ranges::any_of(
            pool, [&e](const std::string& v) { return tag_ci_equal(v, e.value.view()); });
        if (!dupe) pool.emplace_back(e.value.view());
    }

    const std::string_view typed = trimmed(buffer);
    std::vector<std::string> out;
    if (typed.empty()) {
        out = std::move(pool);
        std::ranges::sort(out, [](std::string_view a, std::string_view b) {
            return std::ranges::lexicographical_compare(a, b, [](char x, char y) {
                return std::tolower(static_cast<unsigned char>(x)) <
                       std::tolower(static_cast<unsigned char>(y));
            });
        });
    } else {
        out = tag_suggestions(typed, pool);
    }
    if (out.size() > static_cast<size_t>(TAG_SUGGEST_MAX))
        out.resize(static_cast<size_t>(TAG_SUGGEST_MAX));
    return out;
}

} // namespace ui
