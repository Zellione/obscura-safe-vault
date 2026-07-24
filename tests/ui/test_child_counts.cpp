// Phase 51: sub-gallery tiles state what is DIRECTLY inside them. This
// deliberately differs from the [D] detail panel's recursive count_subtree
// tally — the label wording carries that distinction.

#include "test_framework.h"

#include <string>
#include <vector>

#include "ui/child_counts.h"
#include "vault/index.h"

namespace {

vault::IndexNode gallery(const char* name) {
    vault::IndexNode n; n.type = vault::IndexNode::Type::Gallery; n.name = name; return n;
}
vault::IndexNode image(const char* name) {
    vault::IndexNode n; n.type = vault::IndexNode::Type::Image; n.name = name; return n;
}
vault::IndexNode video(const char* name) {
    vault::IndexNode n; n.type = vault::IndexNode::Type::Video; n.name = name; return n;
}

} // namespace

TEST(direct_child_counts_ignores_nested_content)
{
    auto root = gallery("root");
    auto sub  = gallery("sub");
    sub.children.push_back(image("deep1.jpg"));
    sub.children.push_back(image("deep2.jpg"));
    root.children.push_back(std::move(sub));
    root.children.push_back(image("top.jpg"));

    const auto c = ui::direct_child_counts(root);
    CHECK_EQ(c.galleries, 1);
    CHECK_EQ(c.images, 1);      // NOT 3 — nested images are not counted
    CHECK_EQ(c.videos, 0);
}

TEST(direct_child_counts_counts_videos_separately)
{
    auto root = gallery("root");
    root.children.push_back(video("clip.mp4"));
    root.children.push_back(image("pic.jpg"));
    const auto c = ui::direct_child_counts(root);
    CHECK_EQ(c.videos, 1);
    CHECK_EQ(c.images, 1);
}

TEST(format_tile_counts_combines_media_into_items)
{
    // "items" is images + videos combined: a tile has no room for three numbers,
    // and the [D] panel already breaks them out for anyone who wants the split.
    CHECK_EQ(ui::format_tile_counts({.images = 10, .videos = 2, .galleries = 3}),
             std::string("3 galleries · 12 items"));
}

TEST(format_tile_counts_is_singular_for_one)
{
    CHECK_EQ(ui::format_tile_counts({.images = 1, .videos = 0, .galleries = 1}),
             std::string("1 gallery · 1 item"));
}

TEST(format_tile_counts_drops_a_zero_side)
{
    CHECK_EQ(ui::format_tile_counts({.images = 12, .videos = 0, .galleries = 0}),
             std::string("12 items"));
    CHECK_EQ(ui::format_tile_counts({.images = 0, .videos = 0, .galleries = 4}),
             std::string("4 galleries"));
}

TEST(format_tile_counts_empty_gallery)
{
    CHECK_EQ(ui::format_tile_counts({}), std::string("empty"));
}

TEST(any_tile_counts_to_show_is_false_without_galleries)
{
    const auto a = image("a.jpg");
    const auto b = video("b.mp4");
    const std::vector<const vault::IndexNode*> kids{&a, &b};
    CHECK(!ui::any_tile_counts_to_show(kids));
}

TEST(any_tile_counts_to_show_is_true_with_a_gallery)
{
    const auto a = image("a.jpg");
    const auto g = gallery("sub");
    const std::vector<const vault::IndexNode*> kids{&a, &g};
    CHECK(ui::any_tile_counts_to_show(kids));
}
