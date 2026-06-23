#include "test_framework.h"

#include "ui/gallery_cover.h"
#include "vault/index.h"

using ui::CoverSpan;
using vault::IndexNode;

namespace {
// An image node with a stored thumbnail at (offset, length).
IndexNode img(std::string name, uint64_t off, uint64_t len)
{
    IndexNode n = IndexNode::image(std::move(name));
    n.meta.thumb_offset = off;
    n.meta.thumb_length = len;
    return n;
}

// A video node with a stored poster at (offset, length).
IndexNode vid(std::string name, uint64_t off, uint64_t len)
{
    IndexNode n = IndexNode::video(std::move(name));
    n.vmeta.poster_offset = off;
    n.vmeta.poster_length = len;
    return n;
}
}  // namespace

TEST(cover_leaf_uses_first_image_thumb)
{
    IndexNode g = IndexNode::gallery("leaf");
    g.children.push_back(img("a.jpg", 100, 20));
    g.children.push_back(img("b.jpg", 200, 30));

    const auto c = ui::resolve_single_cover(g);
    CHECK(c.has_value());
    CHECK(*c == (CoverSpan{100, 20}));

    const auto covers = ui::resolve_covers(g);
    CHECK_EQ(static_cast<int>(covers.size()), 1);
    CHECK(covers[0] == (CoverSpan{100, 20}));
}

TEST(cover_leaf_first_child_video_uses_poster)
{
    IndexNode g = IndexNode::gallery("leaf");
    g.children.push_back(vid("clip.mp4", 500, 40));
    g.children.push_back(img("a.jpg", 100, 20));

    const auto c = ui::resolve_single_cover(g);
    CHECK(c.has_value());
    CHECK(*c == (CoverSpan{500, 40}));
}

TEST(cover_leaf_skips_media_without_thumbnail)
{
    IndexNode g = IndexNode::gallery("leaf");
    g.children.push_back(img("a.jpg", 0, 0));    // no thumbnail -> skipped
    g.children.push_back(img("b.jpg", 200, 30));

    const auto c = ui::resolve_single_cover(g);
    CHECK(c.has_value());
    CHECK(*c == (CoverSpan{200, 30}));
}

TEST(cover_folder_of_folders_montage_in_child_order)
{
    // root/{s1{a},s2{b},s3{c}} -> montage of each sub-gallery's first cover.
    IndexNode s1 = IndexNode::gallery("s1");
    s1.children.push_back(img("a.jpg", 11, 1));
    IndexNode s2 = IndexNode::gallery("s2");
    s2.children.push_back(img("b.jpg", 22, 2));
    IndexNode s3 = IndexNode::gallery("s3");
    s3.children.push_back(img("c.jpg", 33, 3));

    IndexNode root = IndexNode::gallery("root");
    root.children.push_back(std::move(s1));
    root.children.push_back(std::move(s2));
    root.children.push_back(std::move(s3));

    const auto covers = ui::resolve_covers(root);
    CHECK_EQ(static_cast<int>(covers.size()), 3);
    CHECK(covers[0] == (CoverSpan{11, 1}));
    CHECK(covers[1] == (CoverSpan{22, 2}));
    CHECK(covers[2] == (CoverSpan{33, 3}));

    // The single representative cover descends the first sub-gallery.
    const auto one = ui::resolve_single_cover(root);
    CHECK(one.has_value());
    CHECK(*one == (CoverSpan{11, 1}));
}

TEST(cover_montage_caps_at_four_subgalleries)
{
    IndexNode root = IndexNode::gallery("root");
    for (uint64_t i = 0; i < 6; ++i) {
        IndexNode s = IndexNode::gallery("s");
        s.children.push_back(img("x.jpg", (i + 1) * 10, 1));
        root.children.push_back(std::move(s));
    }
    const auto covers = ui::resolve_covers(root);
    CHECK_EQ(static_cast<int>(covers.size()), 4);
    CHECK(covers[0] == (CoverSpan{10, 1}));
    CHECK(covers[3] == (CoverSpan{40, 1}));
}

TEST(cover_recurses_through_mixed_depths)
{
    // root/sub/deep/x.jpg — single sub-gallery, two levels down.
    IndexNode deep = IndexNode::gallery("deep");
    deep.children.push_back(img("x.jpg", 77, 7));
    IndexNode sub = IndexNode::gallery("sub");
    sub.children.push_back(std::move(deep));
    IndexNode root = IndexNode::gallery("root");
    root.children.push_back(std::move(sub));

    const auto covers = ui::resolve_covers(root);
    CHECK_EQ(static_cast<int>(covers.size()), 1);
    CHECK(covers[0] == (CoverSpan{77, 7}));
}

TEST(cover_skips_empty_subgalleries)
{
    IndexNode empty1 = IndexNode::gallery("e1");        // nothing inside
    IndexNode empty2 = IndexNode::gallery("e2");
    IndexNode good   = IndexNode::gallery("good");
    good.children.push_back(img("g.jpg", 99, 9));

    IndexNode root = IndexNode::gallery("root");
    root.children.push_back(std::move(empty1));
    root.children.push_back(std::move(good));
    root.children.push_back(std::move(empty2));

    const auto covers = ui::resolve_covers(root);
    CHECK_EQ(static_cast<int>(covers.size()), 1);
    CHECK(covers[0] == (CoverSpan{99, 9}));
}

TEST(cover_empty_gallery_yields_nothing)
{
    const IndexNode g = IndexNode::gallery("empty");
    CHECK(!ui::resolve_single_cover(g).has_value());
    CHECK(ui::resolve_covers(g).empty());
}

TEST(cover_depth_limit_stops_recursion)
{
    // A chain deeper than the allowed depth resolves to nothing rather than
    // overflowing the stack.
    IndexNode leaf = IndexNode::gallery("leaf");
    leaf.children.push_back(img("x.jpg", 5, 5));
    IndexNode chain = std::move(leaf);
    for (int i = 0; i < 4; ++i) {
        IndexNode parent = IndexNode::gallery("p");
        parent.children.push_back(std::move(chain));
        chain = std::move(parent);
    }
    // max_depth small enough that the leaf is out of reach.
    CHECK(!ui::resolve_single_cover(chain, 2).has_value());
    // With ample depth it resolves.
    CHECK(ui::resolve_single_cover(chain, 64).has_value());
}
