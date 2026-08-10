#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "vault/index.h"

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

// Dropdown source for a template-field input (Phase 73): the distinct stored
// values for (category, field) — an entry counts when its field matches ci AND
// its tag's category prefix matches ci. Empty buffer → the whole pool sorted
// ci; a typed buffer ranks via tag_suggestions. Capped at TAG_SUGGEST_MAX.
// Pure — no SDL, no vault, no I/O.
[[nodiscard]] std::vector<std::string> field_value_suggestions(
    std::string_view             buffer,
    std::string_view             category,
    std::string_view             field,
    const vault::VaultSettings&  settings);

} // namespace ui
