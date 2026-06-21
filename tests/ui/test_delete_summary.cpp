#include "test_framework.h"

#include "ui/delete_summary.h"
#include "vault/index.h"

using vault::IndexNode;
using ui::SubtreeCounts;

namespace {
// Build a small gallery tree:
//   root/
//     a.jpg, b.png            (2 images)
//     clip.mp4                (1 video)
//     sub/                    (1 sub-gallery)
//       c.jpg                 (1 image)
//       deep/                 (1 sub-gallery)
//         d.jpg               (1 image)
IndexNode make_tree()
{
    IndexNode deep = IndexNode::gallery("deep");
    deep.children.push_back(IndexNode::image("d.jpg"));

    IndexNode sub = IndexNode::gallery("sub");
    sub.children.push_back(IndexNode::image("c.jpg"));
    sub.children.push_back(std::move(deep));

    IndexNode root = IndexNode::gallery("root");
    root.children.push_back(IndexNode::image("a.jpg"));
    root.children.push_back(IndexNode::image("b.png"));
    root.children.push_back(IndexNode::video("clip.mp4"));
    root.children.push_back(std::move(sub));
    return root;
}
}  // namespace

TEST(delete_summary_counts_nested_subtree)
{
    SubtreeCounts c;
    ui::count_subtree(make_tree(), c);
    CHECK_EQ(c.images, 4);      // a, b, c, d — counted at every depth
    CHECK_EQ(c.videos, 1);      // clip.mp4
    CHECK_EQ(c.galleries, 2);   // sub + deep
}

TEST(delete_summary_empty_gallery_counts_zero)
{
    SubtreeCounts c;
    ui::count_subtree(IndexNode::gallery("empty"), c);
    CHECK_EQ(c.images, 0);
    CHECK_EQ(c.videos, 0);
    CHECK_EQ(c.galleries, 0);
}

TEST(delete_summary_describe_full)
{
    CHECK(ui::describe_subtree({.images = 4, .videos = 1, .galleries = 2})
          == "4 images, 1 video, 2 sub-galleries");
}

TEST(delete_summary_describe_singular_plural)
{
    CHECK(ui::describe_subtree({.images = 1, .videos = 0, .galleries = 1})
          == "1 image, 1 sub-gallery");
}

TEST(delete_summary_describe_drops_zero_counts)
{
    CHECK(ui::describe_subtree({.images = 3, .videos = 0, .galleries = 0}) == "3 images");
    CHECK(ui::describe_subtree({.images = 0, .videos = 2, .galleries = 0}) == "2 videos");
}

TEST(delete_summary_describe_empty_is_nothing)
{
    CHECK(ui::describe_subtree({}) == "nothing");
}
