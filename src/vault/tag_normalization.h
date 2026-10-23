#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>
#include "vault/index.h"

namespace vault {
// Shared by ordinary edits, bundle persistence and preview. Normalization
// leaves mutation-specific count, length and NUL validation to its callers.
[[nodiscard]] inline std::string_view normalise_tag(std::string_view tag) noexcept
{
    const auto first = tag.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return tag.substr(first, tag.find_last_not_of(" \t\r\n") - first + 1);
}

[[nodiscard]] inline std::vector<std::string> normalise_tags(const std::vector<std::string>& input)
{
    std::vector<std::string> out;
    for (const auto& tag : input) {
        const auto trimmed = normalise_tag(tag);
        if (!trimmed.empty() && std::ranges::none_of(out, [&](const auto& existing) {
                return tag_ci_equal(existing, trimmed);
            }))
            out.emplace_back(trimmed);
    }
    return out;
}
}  // namespace vault
