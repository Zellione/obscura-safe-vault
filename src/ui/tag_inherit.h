#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace vault { class Vault; }

namespace ui {

// Case-insensitive ASCII tag comparison — the same identity rule Vault applies
// when de-duplicating tags. Shared by the tag editor and the inherited-tag
// cascade so "tag equality" has one definition in the UI.
[[nodiscard]] bool tag_ci_equal(std::string_view a, std::string_view b);

// Ancestor-gallery tag union for the node at `node_path`: root→parent order,
// case-insensitively de-duplicated, minus the tags the node carries itself
// (own tags win). These are exactly the tags the search cascade matches the
// node by (Phase 12); the tag editor shows them as its read-only "Inherited"
// section so gallery tags are visible on every page (Phase 27 follow-up).
// Pure vault-tree walk over the public Vault::list API — no SDL, no I/O.
[[nodiscard]] std::vector<std::string> inherited_tags(const vault::Vault& vault,
                                                      std::string_view    node_path);

// Descendant tag union for the gallery at `gallery_path`: every tag carried by
// anything beneath it, case-insensitively de-duplicated, minus the gallery's own
// tags AND minus its inherited (ancestor) tags — a tag belongs to exactly one of
// the three read-only sections, never two. The mirror of inherited_tags: nothing
// is stored, so the roll-up self-corrects when a descendant is retagged, moved
// or deleted. Depth-bounded by vault::INDEX_MAX_DEPTH. Pure vault-tree walk over
// the public Vault::list API — no SDL, no I/O. (Phase 51.)
[[nodiscard]] std::vector<std::string> contents_tags(const vault::Vault& vault,
                                                     std::string_view    gallery_path);

} // namespace ui
