// Phase 56: after a background import reallocates the index tree, the viewer
// re-lists its album. If the item on screen is still there, it must be found by
// PATH and its view state left alone — a refit would throw away zoom, pan,
// scroll offset and video playback position on every commit batch.

#include "test_framework.h"

#include <string>
#include <vector>

#include "ui/album_rebind.h"

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
