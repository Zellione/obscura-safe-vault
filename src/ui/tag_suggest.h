#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ui {

// Most suggestions the tag editor's dropdown shows at once (Phase 29).
inline constexpr int TAG_SUGGEST_MAX = 5;

// Autosuggest source for the tag editor (Phase 29): trims `buffer`, ranks the
// vault-wide `vocabulary` via the Phase 18 tag_suggestions (prefix matches
// before substring matches, ci de-dupe keeping the first casing, alphabetical
// ties), hides tags the node already carries (`own_tags`, ci — adding one
// would be a no-op merge), and caps the result at TAG_SUGGEST_MAX. Pure — no
// SDL, no vault, no I/O.
[[nodiscard]] std::vector<std::string> editor_tag_suggestions(
    std::string_view                buffer,
    const std::vector<std::string>& vocabulary,
    const std::vector<std::string>& own_tags);

} // namespace ui
