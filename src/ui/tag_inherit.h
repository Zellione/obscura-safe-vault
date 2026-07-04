#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace vault { class Vault; }

namespace ui {

// Ancestor-gallery tag union for the node at `node_path`: root→parent order,
// case-insensitively de-duplicated, minus the tags the node carries itself
// (own tags win). These are exactly the tags the search cascade matches the
// node by (Phase 12); the tag editor shows them as its read-only "Inherited"
// section so gallery tags are visible on every page (Phase 27 follow-up).
// Pure vault-tree walk over the public Vault::list API — no SDL, no I/O.
[[nodiscard]] std::vector<std::string> inherited_tags(const vault::Vault& vault,
                                                      std::string_view    node_path);

} // namespace ui
