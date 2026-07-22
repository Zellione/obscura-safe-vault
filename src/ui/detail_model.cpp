#include "ui/detail_model.h"

#include <algorithm>
#include <format>
#include <string_view>

#include "ui/delete_summary.h"   // SubtreeCounts, count_subtree, describe_subtree
#include "ui/gallery_sort.h"     // sort_key_label
#include "ui/meta_format.h"
#include "ui/tag_inherit.h"      // tag_ci_equal
#include "vault/index.h"

namespace ui {
namespace {

// The marker line under the heading, e.g. "★ favorite · A animated". Empty when
// the node carries neither marker.
std::string node_markers(const vault::IndexNode& n)
{
    std::string out;
    const auto add = [&out](std::string_view s) {
        if (!out.empty()) { out += " · "; }
        out += s;
    };
    if (n.favorite) { add("★ favorite"); }
    // `animated` lives on ImageMeta only — a video has no such flag.
    if (n.is_image() && n.meta.animated) { add("A animated"); }
    return out;
}

DetailSection image_rows(const vault::IndexNode& n)
{
    return {.title = "",
            .rows  = {{.label = "Type",       .value = std::string(image_format_name(n.meta.format))},
                      {.label = "Size",       .value = format_size(n.meta.orig_size)},
                      {.label = "Dimensions", .value = format_dimensions(n.meta.width, n.meta.height)},
                      {.label = "Added",      .value = format_date(n.meta.created_ts)}},
            .bullets = {}};
}

DetailSection video_rows(const vault::IndexNode& n)
{
    return {.title = "",
            .rows  = {{.label = "Codec",      .value = std::string(video_codec_name(n.vmeta.codec))},
                      {.label = "Container",  .value = std::string(video_container_name(n.vmeta.container))},
                      {.label = "Dimensions", .value = format_dimensions(n.vmeta.width, n.vmeta.height)},
                      {.label = "Length",     .value = format_duration(n.vmeta.duration_us)},
                      {.label = "Size",       .value = format_size(n.vmeta.orig_size)},
                      {.label = "Added",      .value = format_date(n.vmeta.created_ts)}},
            .bullets = {}};
}

DetailSection gallery_rows(const vault::IndexNode& n, vault::SortKey vault_default)
{
    SubtreeCounts c;
    count_subtree(n, c);
    DetailSection s{.title = "",
                    .rows  = {{.label = "Contains",   .value = describe_subtree(c)},
                              {.label = "Total size", .value = format_size(c.bytes)}},
                    .bullets = {}};
    // A gallery following the vault-wide default renders no Sort row at all —
    // the breadcrumb uses the same rule, so the two never disagree.
    if (auto label = sort_key_label(n.sort_key, vault_default); !label.empty()) {
        s.rows.push_back({.label = "Sort", .value = std::move(label)});
    }
    return s;
}

// Case-insensitive intersection of every node's own tags, in the first node's
// order and casing. Empty when any node lacks one of the first node's tags.
std::vector<std::string> shared_tags(std::span<const vault::IndexNode* const> nodes)
{
    const vault::IndexNode* first = nullptr;
    for (const auto* n : nodes) {
        if (n != nullptr) { first = n; break; }
    }
    if (first == nullptr) {
        return {};
    }

    std::vector<std::string> out;
    for (const auto& tag : first->tags) {
        const bool in_all = std::ranges::all_of(nodes, [&tag](const vault::IndexNode* n) {
            return n == nullptr || std::ranges::any_of(
                       n->tags, [&tag](const std::string& t) { return tag_ci_equal(tag, t); });
        });
        if (in_all) {
            out.push_back(tag);
        }
    }
    return out;
}

}  // namespace

void append_tag_sections(DetailContent&               out,
                         std::span<const std::string> own,
                         std::span<const std::string> inherited,
                         std::string_view             own_title)
{
    if (!own.empty()) {
        out.sections.push_back({.title   = std::string(own_title),
                                .rows    = {},
                                .bullets = std::vector<std::string>(own.begin(), own.end()),
                                .is_tags = true});
    }
    if (!inherited.empty()) {
        out.sections.push_back({.title   = "Inherited",
                                .rows    = {},
                                .bullets = std::vector<std::string>(inherited.begin(), inherited.end()),
                                .is_tags = true});
    }
}

DetailContent build_node_details(const vault::IndexNode&      node,
                                 std::span<const std::string> inherited,
                                 vault::SortKey               vault_default)
{
    DetailContent out{.heading = node.name, .subheading = node_markers(node), .sections = {}};
    if (node.is_image()) {
        out.sections.push_back(image_rows(node));
    } else if (node.is_video()) {
        out.sections.push_back(video_rows(node));
    } else {
        out.sections.push_back(gallery_rows(node, vault_default));
    }
    append_tag_sections(out, node.tags, inherited, "Tags");
    return out;
}

DetailContent build_selection_details(std::span<const vault::IndexNode* const> nodes,
                                      std::span<const std::string>            inherited)
{
    DetailContent out;
    out.heading = std::format("{} item{} selected", nodes.size(), nodes.size() == 1 ? "" : "s");
    if (nodes.empty()) {
        return out;
    }

    // A selected gallery counts itself and contributes its whole subtree, so the
    // totals answer "what would an export/delete of this selection touch?".
    SubtreeCounts tally;
    for (const auto* n : nodes) {
        if (n == nullptr) {
            continue;
        }
        if (n->is_gallery())    { ++tally.galleries; count_subtree(*n, tally); }
        else if (n->is_video()) { ++tally.videos; tally.bytes += n->vmeta.orig_size; }
        else                    { ++tally.images; tally.bytes += n->meta.orig_size; }
    }
    out.sections.push_back({.title   = "",
                            .rows    = {{.label = "Contains",   .value = describe_subtree(tally)},
                                        {.label = "Total size", .value = format_size(tally.bytes)}},
                            .bullets = {}});

    auto shared = shared_tags(nodes);
    if (shared.empty()) {
        shared.emplace_back("no shared tags");
    }
    append_tag_sections(out, shared, inherited, "Tags (shared)");
    return out;
}

}  // namespace ui
