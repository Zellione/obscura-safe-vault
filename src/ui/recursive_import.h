#pragma once

// Phase 53: recursive extraction of archives nested inside archives.
//
// This header currently carries the naming rules; the depth-first orchestrator
// and its guards land in the same module.
//
// Pure: no miniz, no libarchive, no vault, no SDL.

#include <string>
#include <string_view>
#include <vector>

namespace ui {

// Sub-gallery name for a nested archive: extension stripped, then run through
// vault::sanitize_node_name (invariant 6 — an archive entry name is untrusted
// and becomes a path component). "bonus.tar.gz" -> "bonus", not "bonus.tar".
//
// Returns "" when nothing usable survives; the caller substitutes a fallback
// rather than creating an unnamed gallery.
[[nodiscard]] std::string nested_gallery_name(std::string_view archive_filename);

// `base` if unused, else `base_2`, `base_3`, … skipping anything already in
// `taken`. Comparison is exact — it mirrors Vault::create_gallery's duplicate
// rule, which is what would otherwise reject the second sub-gallery.
[[nodiscard]] std::string unique_gallery_name(std::string_view                base,
                                              const std::vector<std::string>& taken);

} // namespace ui
