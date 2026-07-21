#include "ui/detail_model.h"

#include <string_view>

#include "ui/delete_summary.h"   // SubtreeCounts, count_subtree, describe_subtree
#include "ui/gallery_sort.h"     // sort_key_label
#include "ui/meta_format.h"
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

DetailSection gallery_rows(const vault::IndexNode& n)
{
    SubtreeCounts c;
    count_subtree(n, c);
    DetailSection s{.title = "",
                    .rows  = {{.label = "Contains",   .value = describe_subtree(c)},
                              {.label = "Total size", .value = format_size(c.bytes)}},
                    .bullets = {}};
    // Manual is the default and renders as an empty label — the breadcrumb uses
    // the same rule, so an unsorted gallery shows no Sort row at all.
    if (auto label = sort_key_label(n.sort_key); !label.empty()) {
        s.rows.push_back({.label = "Sort", .value = std::move(label)});
    }
    return s;
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
                                .bullets = {own.begin(), own.end()}});
    }
    if (!inherited.empty()) {
        out.sections.push_back({.title   = "Inherited",
                                .rows    = {},
                                .bullets = {inherited.begin(), inherited.end()}});
    }
}

DetailContent build_node_details(const vault::IndexNode&      node,
                                 std::span<const std::string> inherited)
{
    DetailContent out{.heading = node.name, .subheading = node_markers(node), .sections = {}};
    if (node.is_image()) {
        out.sections.push_back(image_rows(node));
    } else if (node.is_video()) {
        out.sections.push_back(video_rows(node));
    } else {
        out.sections.push_back(gallery_rows(node));
    }
    append_tag_sections(out, node.tags, inherited, "Tags");
    return out;
}

}  // namespace ui
