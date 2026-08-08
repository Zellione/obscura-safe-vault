// Phase 56: after a background import reallocates the index tree, the viewer
// re-lists its album. If the item on screen is still there, it must be found by
// PATH and its view state left alone — a refit would throw away zoom, pan,
// scroll offset and video playback position on every commit batch.

#include "test_framework.h"

#include <string>
#include <vector>

#include "ui/album_rebind.h"
#include "vault/index.h"

using vault::IndexNode;

TEST(rebind_follows_the_item_when_a_node_is_inserted_before_it)
{
    const std::vector<std::string> after{"g/a.jpg", "g/new.jpg", "g/b.jpg"};
    const ui::AlbumRebind r = ui::rebind_album_index(after, "g/b.jpg", 1);
    CHECK_EQ(r.index, 2);
    CHECK(r.preserve);
}

TEST(rebind_is_a_no_op_when_the_item_did_not_move)
{
    const std::vector<std::string> after{"g/a.jpg", "g/b.jpg"};
    const ui::AlbumRebind r = ui::rebind_album_index(after, "g/b.jpg", 1);
    CHECK_EQ(r.index, 1);
    CHECK(r.preserve);
}

TEST(rebind_refits_when_the_item_is_gone)
{
    const std::vector<std::string> after{"g/a.jpg", "g/c.jpg"};
    const ui::AlbumRebind r = ui::rebind_album_index(after, "g/b.jpg", 1);
    CHECK_FALSE(r.preserve);
    CHECK_EQ(r.index, 1);     // clamped, then the caller refits
}

TEST(rebind_clamps_a_stale_index_into_a_shrunken_album)
{
    const std::vector<std::string> after{"g/a.jpg"};
    const ui::AlbumRebind r = ui::rebind_album_index(after, "g/b.jpg", 5);
    CHECK_FALSE(r.preserve);
    CHECK_EQ(r.index, 0);
}

TEST(rebind_reports_an_empty_album_without_a_negative_index)
{
    const ui::AlbumRebind r = ui::rebind_album_index({}, "g/b.jpg", 3);
    CHECK_FALSE(r.preserve);
    CHECK_EQ(r.index, 0);
}

TEST(rebind_matches_the_whole_path_not_a_prefix)
{
    const std::vector<std::string> after{"g/b.jpg.bak", "g/b.jpg"};
    const ui::AlbumRebind r = ui::rebind_album_index(after, "g/b.jpg", 0);
    CHECK_EQ(r.index, 1);
    CHECK(r.preserve);
}

TEST(compact_album_drops_null_pairs_in_order)
{
    vault::IndexNode a = vault::IndexNode::image("a");
    vault::IndexNode c = vault::IndexNode::image("c");
    std::vector<const vault::IndexNode*> images{&a, nullptr, &c, nullptr};
    std::vector<std::string>             paths{"g/a", "g/b", "g/c", "g/d"};

    const size_t removed = ui::compact_album(images, paths);

    CHECK_EQ(removed, 2u);
    REQUIRE(images.size() == 2u);
    CHECK(images[0] == &a); CHECK(images[1] == &c);
    REQUIRE(paths.size() == 2u);
    CHECK_EQ(paths[0], "g/a"); CHECK_EQ(paths[1], "g/c");
}

TEST(compact_album_all_null_empties_both)
{
    std::vector<const vault::IndexNode*> images{nullptr};
    std::vector<std::string>             paths{"g/x"};
    CHECK_EQ(ui::compact_album(images, paths), 1u);
    CHECK(images.empty()); CHECK(paths.empty());
}

// Phase 46 made galleries mixed: sub-galleries and media are siblings, and the
// sorted listing partitions galleries FIRST. The grid/search screens index that
// full listing, while the viewer's album holds media only — the two index
// spaces differ by the number of sub-galleries. These helpers convert at the
// boundary; without them, activating a video (or image) behind N sub-galleries
// opened the item N positions later.

TEST(media_index_skips_sub_galleries_before_the_item)
{
    IndexNode g1 = IndexNode::gallery("sub1");
    IndexNode g2 = IndexNode::gallery("sub2");
    IndexNode a = IndexNode::image("a.jpg");
    IndexNode v = IndexNode::video("v.mp4");
    IndexNode b = IndexNode::image("b.jpg");
    const std::vector<const IndexNode*> listing{&g1, &g2, &a, &v, &b};

    CHECK_EQ(ui::media_index_in_listing(listing, 3), 1);  // the video
    CHECK_EQ(ui::media_index_in_listing(listing, 2), 0);  // first image
    CHECK_EQ(ui::media_index_in_listing(listing, 4), 2);  // last image
}

TEST(media_index_is_identity_without_sub_galleries)
{
    IndexNode a = IndexNode::image("a.jpg");
    IndexNode v = IndexNode::video("v.mp4");
    const std::vector<const IndexNode*> listing{&a, &v};

    CHECK_EQ(ui::media_index_in_listing(listing, 0), 0);
    CHECK_EQ(ui::media_index_in_listing(listing, 1), 1);
}

TEST(media_index_clamps_out_of_range_and_empty)
{
    IndexNode g = IndexNode::gallery("sub");
    IndexNode a = IndexNode::image("a.jpg");
    const std::vector<const IndexNode*> listing{&g, &a};

    CHECK_EQ(ui::media_index_in_listing(listing, -1), 0);
    CHECK_EQ(ui::media_index_in_listing(listing, 99), 0);  // one media item → clamped to it
    CHECK_EQ(ui::media_index_in_listing({}, 0), 0);
}

TEST(media_index_on_a_gallery_entry_lands_on_the_next_media)
{
    IndexNode g = IndexNode::gallery("sub");
    IndexNode a = IndexNode::image("a.jpg");
    const std::vector<const IndexNode*> listing{&g, &a};

    CHECK_EQ(ui::media_index_in_listing(listing, 0), 0);  // defensive: not a media tile
}

TEST(listing_index_restores_the_sub_gallery_offset)
{
    IndexNode g1 = IndexNode::gallery("sub1");
    IndexNode g2 = IndexNode::gallery("sub2");
    IndexNode a = IndexNode::image("a.jpg");
    IndexNode v = IndexNode::video("v.mp4");
    const std::vector<const IndexNode*> listing{&g1, &g2, &a, &v};

    CHECK_EQ(ui::listing_index_of_media(listing, 0), 2);
    CHECK_EQ(ui::listing_index_of_media(listing, 1), 3);
}

TEST(listing_index_clamps_out_of_range_and_empty)
{
    IndexNode g = IndexNode::gallery("sub");
    IndexNode a = IndexNode::image("a.jpg");
    const std::vector<const IndexNode*> listing{&g, &a};

    CHECK_EQ(ui::listing_index_of_media(listing, 99), 1);  // clamped to the last media
    CHECK_EQ(ui::listing_index_of_media(listing, -1), 1);
    CHECK_EQ(ui::listing_index_of_media({}, 0), 0);

    const std::vector<const IndexNode*> only_galleries{&g};
    CHECK_EQ(ui::listing_index_of_media(only_galleries, 0), 0);  // no media at all
}
