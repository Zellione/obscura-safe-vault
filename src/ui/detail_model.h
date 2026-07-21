#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vault { struct IndexNode; }

// Pure content model for the gallery detail panel (Phase 48). Turns an index
// node into an ordered, render-agnostic description: a heading, an optional
// marker line, and a list of sections. SDL-, gfx- and IO-free so it can be unit
// tested headlessly; `detail_panel.*` owns the drawing.
namespace ui {

// One "label: value" line, e.g. {"Codec", "H.264"}.
struct DetailRow {
    std::string label;
    std::string value;
};

// A titled block. `title` is empty for the leading metadata block (its rows are
// self-labelling). A section carries rows or bullets, never both.
struct DetailSection {
    std::string              title;
    std::vector<DetailRow>   rows;
    std::vector<std::string> bullets;   // tag lists
};

struct DetailContent {
    std::string                heading;      // node name, or "N items selected"
    std::string                subheading;   // marker line ("★ favorite"); may be empty
    std::vector<DetailSection> sections;
};

// Append the "Tags" (or `own_title`) and "Inherited" bullet sections to `out`,
// skipping whichever list is empty. Shared by the single-node and aggregate
// builders so both order and omit tag blocks identically.
void append_tag_sections(DetailContent&               out,
                         std::span<const std::string> own,
                         std::span<const std::string> inherited,
                         std::string_view             own_title);

// Describe one node. `inherited` is the ancestor-gallery tag cascade for the
// node (see ui::inherited_tags); pass an empty span when there is none. A
// section with nothing to say is omitted rather than emitted empty.
[[nodiscard]] DetailContent build_node_details(const vault::IndexNode&      node,
                                               std::span<const std::string> inherited);

// Describe a multi-item selection: per-kind counts, summed size, and the tags
// every selected item carries. Selected items are always siblings, so they
// share one `inherited` cascade. Null pointers in `nodes` are skipped.
[[nodiscard]] DetailContent build_selection_details(
        std::span<const vault::IndexNode* const> nodes,
        std::span<const std::string>            inherited);

} // namespace ui
