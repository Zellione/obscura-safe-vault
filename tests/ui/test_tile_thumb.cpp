#include "test_framework.h"

#include "ui/tile_thumb.h"
#include "vault/index.h"

using ui::thumb_key_for;
using vault::IndexNode;

TEST(thumb_key_image_with_thumbnail_uses_data_offset)
{
    IndexNode n = IndexNode::image("a.jpg");
    n.meta.data_offset  = 111;
    n.meta.thumb_length = 20;
    const auto k = thumb_key_for(n);
    CHECK_EQ(k.key, static_cast<uint64_t>(111));
    CHECK(k.present);
}

TEST(thumb_key_image_without_thumbnail_is_absent)
{
    IndexNode n = IndexNode::image("a.jpg");
    n.meta.data_offset  = 111;
    n.meta.thumb_length = 0;
    CHECK_FALSE(thumb_key_for(n).present);
}

TEST(thumb_key_video_uses_poster_offset_and_length)
{
    IndexNode n = IndexNode::video("clip.mp4");
    n.vmeta.poster_offset = 500;
    n.vmeta.poster_length = 40;
    const auto k = thumb_key_for(n);
    CHECK_EQ(k.key, static_cast<uint64_t>(500));
    CHECK(k.present);
}

TEST(thumb_key_video_regression_previously_always_reported_absent)
{
    // Before the fix, both call sites gated on node.meta.thumb_length alone,
    // which is always 0 for a video node — every video silently reported "no
    // thumbnail" no matter what its poster held. Prove the old field really
    // is 0 here, and that thumb_key_for still reports present=true anyway.
    IndexNode n = IndexNode::video("clip.mp4");
    n.vmeta.poster_offset = 500;
    n.vmeta.poster_length = 40;
    CHECK_EQ(n.meta.thumb_length, static_cast<uint64_t>(0));
    CHECK(thumb_key_for(n).present);
}

TEST(thumb_key_video_without_poster_is_absent)
{
    IndexNode n = IndexNode::video("clip.mp4");
    n.vmeta.poster_offset = 0;
    n.vmeta.poster_length = 0;
    CHECK_FALSE(thumb_key_for(n).present);
}

TEST(tile_badge_shown_for_an_animated_gif)
{
    vault::IndexNode n = vault::IndexNode::image("loop.gif");
    n.meta.format   = vault::ImageFormat::GIF;
    n.meta.animated = true;
    CHECK(ui::tile_shows_animated_badge(n));
}

TEST(tile_badge_hidden_for_a_still_gif)
{
    vault::IndexNode n = vault::IndexNode::image("still.gif");
    n.meta.format   = vault::ImageFormat::GIF;
    n.meta.animated = false;
    CHECK(!ui::tile_shows_animated_badge(n));
}

TEST(tile_badge_hidden_for_a_jpeg)
{
    vault::IndexNode n = vault::IndexNode::image("photo.jpg");
    n.meta.format = vault::ImageFormat::JPEG;
    CHECK(!ui::tile_shows_animated_badge(n));
}

TEST(tile_badge_hidden_for_a_gallery)
{
    const vault::IndexNode n = vault::IndexNode::gallery("album");
    CHECK(!ui::tile_shows_animated_badge(n));
}

TEST(tile_badge_hidden_for_a_video)
{
    const vault::IndexNode n = vault::IndexNode::video("clip.mp4");
    CHECK(!ui::tile_shows_animated_badge(n));
}
