#include "test_framework.h"

#include "ui/detail_model.h"
#include "vault/index.h"

#include <algorithm>
#include <string>
#include <vector>

using vault::IndexNode;

namespace {

// Find a row's value by label across every section; "" when absent.
std::string row_value(const ui::DetailContent& c, std::string_view label)
{
    for (const auto& s : c.sections) {
        for (const auto& r : s.rows) {
            if (r.label == label) { return r.value; }
        }
    }
    return "";
}

// Find a section by title; nullptr when absent.
const ui::DetailSection* section(const ui::DetailContent& c, std::string_view title)
{
    const auto it = std::ranges::find_if(
        c.sections, [title](const ui::DetailSection& s) { return s.title == title; });
    return it == c.sections.end() ? nullptr : &*it;
}

IndexNode make_image()
{
    IndexNode n = IndexNode::image("kyoto.jpg");
    n.meta.format     = vault::ImageFormat::JPEG;
    n.meta.width      = 4032;
    n.meta.height     = 3024;
    n.meta.orig_size  = 3'250'586;
    n.meta.created_ts = 1'715'385'600;   // 2024-05-11 UTC
    return n;
}

IndexNode make_video()
{
    IndexNode n = IndexNode::video("clip.mp4");
    n.vmeta.container   = vault::VideoContainer::MP4;
    n.vmeta.codec       = vault::VideoCodec::H264;
    n.vmeta.width       = 1920;
    n.vmeta.height      = 1080;
    n.vmeta.duration_us = 134'000'000;   // 2:14
    n.vmeta.orig_size   = 92'485'386;
    n.vmeta.created_ts  = 1'715'385'600;
    return n;
}

}  // namespace

TEST(detail_image_field_set)
{
    const auto c = ui::build_node_details(make_image(), {});
    CHECK(c.heading == "kyoto.jpg");
    CHECK(row_value(c, "Type") == "JPEG");
    CHECK(row_value(c, "Dimensions") == "4032x3024");
    CHECK(row_value(c, "Size") == "3.1 MB");
    CHECK(row_value(c, "Added") == "2024-05-11");
}

TEST(detail_video_field_set)
{
    const auto c = ui::build_node_details(make_video(), {});
    CHECK(c.heading == "clip.mp4");
    CHECK(row_value(c, "Codec") == "H.264");
    CHECK(row_value(c, "Container") == "MP4");
    CHECK(row_value(c, "Dimensions") == "1920x1080");
    CHECK(row_value(c, "Length") == "2:14");
    CHECK(row_value(c, "Size") == "88.2 MB");
}

TEST(detail_video_unknown_codec_reads_video)
{
    // A video imported before its codec was decodable sits at Unknown until
    // video_repair heals it on the next gallery open.
    IndexNode n = make_video();
    n.vmeta.codec = vault::VideoCodec::Unknown;
    CHECK(ui::build_node_details(n, {}).sections.at(0).rows.at(0).value == "Video");
}

TEST(detail_zero_dimensions_and_date_render_dash)
{
    IndexNode n = IndexNode::image("bare.png");
    n.meta.format = vault::ImageFormat::PNG;
    const auto c = ui::build_node_details(n, {});
    CHECK(row_value(c, "Dimensions") == "-");
    CHECK(row_value(c, "Added") == "-");
}

TEST(detail_favorite_and_animated_markers)
{
    IndexNode n = make_image();
    n.favorite       = true;
    n.meta.animated  = true;
    const auto c = ui::build_node_details(n, {});
    CHECK(c.subheading.contains("favorite"));
    CHECK(c.subheading.contains("animated"));
}

TEST(detail_no_markers_leaves_subheading_empty)
{
    CHECK(ui::build_node_details(make_image(), {}).subheading.empty());
}

TEST(detail_animated_marker_is_image_only)
{
    // vmeta has no `animated`; a video must never claim one.
    IndexNode n = make_video();
    n.favorite = true;
    const auto c = ui::build_node_details(n, {});
    CHECK(!c.subheading.contains("animated"));
}

TEST(detail_own_and_inherited_tags_are_separate_sections)
{
    IndexNode n = make_image();
    n.tags = {"travel", "kyoto"};
    const std::vector<std::string> inherited{"japan", "holiday"};
    const auto c = ui::build_node_details(n, inherited);

    const auto* own = section(c, "Tags");
    REQUIRE(own != nullptr);
    CHECK_EQ(own->bullets.size(), static_cast<std::size_t>(2));
    CHECK(own->bullets.at(0) == "travel");

    const auto* inh = section(c, "Inherited");
    REQUIRE(inh != nullptr);
    CHECK_EQ(inh->bullets.size(), static_cast<std::size_t>(2));
    CHECK(inh->bullets.at(0) == "japan");
}

TEST(detail_empty_tag_lists_omit_their_sections)
{
    const auto c = ui::build_node_details(make_image(), {});
    CHECK(section(c, "Tags") == nullptr);
    CHECK(section(c, "Inherited") == nullptr);
}

TEST(detail_gallery_tally_and_total_size)
{
    IndexNode a = IndexNode::image("a.jpg");
    a.meta.orig_size = 1000;
    IndexNode clip = IndexNode::video("clip.mp4");
    clip.vmeta.orig_size = 2000;
    IndexNode sub = IndexNode::gallery("sub");
    sub.children.push_back(std::move(clip));

    IndexNode g = IndexNode::gallery("Japan");
    g.children.push_back(std::move(a));
    g.children.push_back(std::move(sub));

    const auto c = ui::build_node_details(g, {});
    CHECK(c.heading == "Japan");
    CHECK(row_value(c, "Contains") == "1 image, 1 video, 1 sub-gallery");
    CHECK(row_value(c, "Total size") == "2.9 KB");   // 3000 bytes
}

TEST(detail_empty_gallery_reads_nothing)
{
    const auto c = ui::build_node_details(IndexNode::gallery("empty"), {});
    CHECK(row_value(c, "Contains") == "nothing");
    CHECK(row_value(c, "Total size") == "0 B");
}

TEST(detail_gallery_sort_row_shown_only_when_not_manual)
{
    IndexNode g = IndexNode::gallery("Japan");

    g.sort_key = vault::SortKey::Manual;
    CHECK(row_value(ui::build_node_details(g, {}), "Sort").empty());

    g.sort_key = vault::SortKey::NameAsc;
    CHECK(!row_value(ui::build_node_details(g, {}), "Sort").empty());
}

TEST(detail_selection_counts_and_total_size)
{
    IndexNode a = make_image();          // 3'250'586 bytes
    IndexNode v = make_video();          // 92'485'386 bytes
    IndexNode inner = IndexNode::image("inner.jpg");
    inner.meta.orig_size = 1024;
    IndexNode g = IndexNode::gallery("sub");
    g.children.push_back(std::move(inner));

    const std::vector<const IndexNode*> sel{&a, &v, &g};
    const auto c = ui::build_selection_details(sel, {});

    CHECK(c.heading == "3 items selected");
    // The selected gallery counts itself AND contributes its subtree.
    CHECK(row_value(c, "Contains") == "2 images, 1 video, 1 sub-gallery");
    CHECK(row_value(c, "Total size") == "91.3 MB");   // 3250586 + 92485386 + 1024
}

TEST(detail_selection_single_item_is_singular)
{
    IndexNode a = make_image();
    const std::vector<const IndexNode*> sel{&a};
    CHECK(ui::build_selection_details(sel, {}).heading == "1 item selected");
}

TEST(detail_selection_empty)
{
    const auto c = ui::build_selection_details({}, {});
    CHECK(c.heading == "0 items selected");
    CHECK(c.sections.empty());
}

TEST(detail_selection_shared_tags_are_the_intersection)
{
    IndexNode a = make_image();
    a.tags = {"travel", "kyoto", "2024"};
    IndexNode b = make_image();
    b.tags = {"travel", "osaka", "2024"};

    const std::vector<const IndexNode*> sel{&a, &b};
    const auto* shared = section(ui::build_selection_details(sel, {}), "Tags (shared)");
    REQUIRE(shared != nullptr);
    CHECK_EQ(shared->bullets.size(), static_cast<std::size_t>(2));
    CHECK(shared->bullets.at(0) == "travel");
    CHECK(shared->bullets.at(1) == "2024");
}

TEST(detail_selection_shared_tags_are_case_insensitive)
{
    IndexNode a = make_image();
    a.tags = {"Travel"};
    IndexNode b = make_image();
    b.tags = {"travel"};

    const std::vector<const IndexNode*> sel{&a, &b};
    const auto* shared = section(ui::build_selection_details(sel, {}), "Tags (shared)");
    REQUIRE(shared != nullptr);
    CHECK_EQ(shared->bullets.size(), static_cast<std::size_t>(1));
    CHECK(shared->bullets.at(0) == "Travel");   // first node's casing wins
}

TEST(detail_selection_no_overlap_says_so)
{
    IndexNode a = make_image();
    a.tags = {"kyoto"};
    IndexNode b = make_image();
    b.tags = {"osaka"};

    const std::vector<const IndexNode*> sel{&a, &b};
    const auto* shared = section(ui::build_selection_details(sel, {}), "Tags (shared)");
    REQUIRE(shared != nullptr);
    CHECK_EQ(shared->bullets.size(), static_cast<std::size_t>(1));
    CHECK(shared->bullets.at(0) == "no shared tags");
}

TEST(detail_selection_untagged_items_say_no_shared_tags)
{
    IndexNode a = make_image();
    IndexNode b = make_image();
    const std::vector<const IndexNode*> sel{&a, &b};
    const auto* shared = section(ui::build_selection_details(sel, {}), "Tags (shared)");
    REQUIRE(shared != nullptr);
    CHECK(shared->bullets.at(0) == "no shared tags");
}

TEST(detail_selection_shows_inherited_once)
{
    IndexNode a = make_image();
    IndexNode b = make_image();
    const std::vector<const IndexNode*> sel{&a, &b};
    const std::vector<std::string> inherited{"japan"};
    const auto* inh = section(ui::build_selection_details(sel, inherited), "Inherited");
    REQUIRE(inh != nullptr);
    CHECK_EQ(inh->bullets.size(), static_cast<std::size_t>(1));
}
