#include "ui/tag_category.h"

#include <algorithm>

#include "ui/tag_inherit.h"   // tag_ci_equal — the UI's one definition of tag equality

namespace ui {

TagDisplay resolve_tag(std::string_view tag,
                       std::span<const vault::TagCategory> categories) noexcept
{
    const size_t colon = tag.find(':');
    if (colon == std::string_view::npos) {
        return {.text = tag, .swatch = -1};
    }

    const std::string_view prefix = tag.substr(0, colon);
    const std::string_view suffix = tag.substr(colon + 1);
    if (prefix.empty() || suffix.empty()) {
        return {.text = tag, .swatch = -1};
    }

    const auto it = std::ranges::find_if(categories, [prefix](const vault::TagCategory& c) {
        return tag_ci_equal(c.name, prefix);
    });
    if (it == categories.end()) {
        return {.text = tag, .swatch = -1};
    }

    return {.text = suffix, .swatch = static_cast<int>(it->swatch)};
}

} // namespace ui
