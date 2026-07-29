#include "test_framework.h"

#include "ui/tile_thumb.h"
#include "vault/index.h"

using ui::thumb_key_for;
using vault::IndexNode;

TEST(thumb_key_image_with_thumbnail_uses_data_offset)
{
    IndexNode n = IndexNode::image("a.jpg");
    n.meta.data_offset  = 111;
    n.meta.thumb_offset = 222;
    n.meta.thumb_length = 20;
    const auto k = thumb_key_for(n);
    CHECK_EQ(k.key, static_cast<uint64_t>(111));
    CHECK_EQ(k.offset, static_cast<uint64_t>(222));
    CHECK_EQ(k.length, static_cast<uint64_t>(20));
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
    CHECK_EQ(k.offset, static_cast<uint64_t>(500));
    CHECK_EQ(k.length, static_cast<uint64_t>(40));
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

TEST(tile_can_hover_animate_true_for_animated_gif_within_budget)
{
    vault::IndexNode n = vault::IndexNode::image("loop.gif");
    n.meta.format   = vault::ImageFormat::GIF;
    n.meta.animated = true;
    n.meta.width    = 320;
    n.meta.height   = 240;
    CHECK(ui::tile_can_hover_animate(n));
}

TEST(tile_can_hover_animate_false_for_animated_gif_exceeding_width)
{
    vault::IndexNode n = vault::IndexNode::image("big.gif");
    n.meta.format   = vault::ImageFormat::GIF;
    n.meta.animated = true;
    n.meta.width    = 4000;
    n.meta.height   = 240;
    CHECK(!ui::tile_can_hover_animate(n));
}

TEST(tile_can_hover_animate_false_for_animated_gif_exceeding_height)
{
    vault::IndexNode n = vault::IndexNode::image("tall.gif");
    n.meta.format   = vault::ImageFormat::GIF;
    n.meta.animated = true;
    n.meta.width    = 320;
    n.meta.height   = 5000;
    CHECK(!ui::tile_can_hover_animate(n));
}

TEST(tile_can_hover_animate_false_for_zero_width_gif)
{
    vault::IndexNode n = vault::IndexNode::image("bad.gif");
    n.meta.format   = vault::ImageFormat::GIF;
    n.meta.animated = true;
    n.meta.width    = 0;
    n.meta.height   = 240;
    CHECK(!ui::tile_can_hover_animate(n));
}

TEST(tile_can_hover_animate_false_for_zero_height_gif)
{
    vault::IndexNode n = vault::IndexNode::image("bad.gif");
    n.meta.format   = vault::ImageFormat::GIF;
    n.meta.animated = true;
    n.meta.width    = 320;
    n.meta.height   = 0;
    CHECK(!ui::tile_can_hover_animate(n));
}

TEST(tile_can_hover_animate_false_for_still_gif)
{
    vault::IndexNode n = vault::IndexNode::image("still.gif");
    n.meta.format   = vault::ImageFormat::GIF;
    n.meta.animated = false;
    n.meta.width    = 320;
    n.meta.height   = 240;
    CHECK(!ui::tile_can_hover_animate(n));
}

TEST(tile_can_hover_animate_false_for_jpeg)
{
    vault::IndexNode n = vault::IndexNode::image("photo.jpg");
    n.meta.format = vault::ImageFormat::JPEG;
    n.meta.width  = 320;
    n.meta.height = 240;
    CHECK(!ui::tile_can_hover_animate(n));
}

TEST(tile_can_hover_animate_false_for_gallery)
{
    vault::IndexNode n = vault::IndexNode::gallery("album");
    CHECK(!ui::tile_can_hover_animate(n));
}

TEST(tile_can_hover_animate_false_for_video)
{
    vault::IndexNode n = vault::IndexNode::video("clip.mp4");
    CHECK(!ui::tile_can_hover_animate(n));
}

// --- Animated WebP (Phase 57) -------------------------------------------------
// The badge and the hover gate defer to vault::format_can_animate, so WebP is
// treated exactly like GIF and a format that cannot animate never badges.

TEST(tile_badge_shown_for_an_animated_webp)
{
    vault::IndexNode n = vault::IndexNode::image("loop.webp");
    n.meta.format   = vault::ImageFormat::WebP;
    n.meta.animated = true;
    CHECK(ui::tile_shows_animated_badge(n));
}

TEST(tile_badge_hidden_for_a_still_webp)
{
    vault::IndexNode n = vault::IndexNode::image("still.webp");
    n.meta.format   = vault::ImageFormat::WebP;
    n.meta.animated = false;
    CHECK(!ui::tile_shows_animated_badge(n));
}

TEST(tile_badge_hidden_for_a_flagged_non_animatable_format)
{
    // Defence in depth: a stale or hostile flag on a format that cannot animate
    // must not produce a badge the viewer can never honour.
    vault::IndexNode n = vault::IndexNode::image("photo.heic");
    n.meta.format   = vault::ImageFormat::HEIC;
    n.meta.animated = true;
    CHECK(!ui::tile_shows_animated_badge(n));
}

TEST(tile_can_hover_animate_true_for_animated_webp_within_budget)
{
    vault::IndexNode n = vault::IndexNode::image("loop.webp");
    n.meta.format   = vault::ImageFormat::WebP;
    n.meta.animated = true;
    n.meta.width    = 320;
    n.meta.height   = 240;
    CHECK(ui::tile_can_hover_animate(n));
}

TEST(tile_can_hover_animate_false_for_animated_webp_exceeding_budget)
{
    vault::IndexNode n = vault::IndexNode::image("big.webp");
    n.meta.format   = vault::ImageFormat::WebP;
    n.meta.animated = true;
    n.meta.width    = 4000;
    n.meta.height   = 240;
    CHECK(!ui::tile_can_hover_animate(n));
}
