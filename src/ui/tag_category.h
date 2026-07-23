#pragma once

#include <span>
#include <string_view>

#include "vault/index.h"

// Phase 49: how a stored tag is DISPLAYED. Storage and matching are unchanged —
// search, the advanced query, autosuggest and the tag editor's input all keep
// operating on the full "artist:Kaguya" string. This header only decides what
// the user sees and in which colour.
namespace ui {

// Resolution of one stored tag. `text` borrows from the `tag` argument, so it
// must not outlive it. `swatch` is an index into gfx's fixed palette, or < 0 for
// an uncategorised tag (drawn in TEXT_DIM).
struct TagDisplay {
    std::string_view text;
    int              swatch = -1;
};

// Split `tag` at its FIRST ':'. If the prefix matches a configured category
// (case-insensitively, ASCII) AND the suffix is non-empty, return the suffix
// plus that category's swatch. Otherwise return `tag` verbatim, uncategorised —
// which is what keeps "12:30" and "Re:Zero" intact and makes an unconfigured
// prefix opt-in rather than automatic.
[[nodiscard]] TagDisplay resolve_tag(std::string_view tag,
                                     std::span<const vault::TagCategory> categories) noexcept;

} // namespace ui
