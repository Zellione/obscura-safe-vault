#include "test_framework.h"

#ifdef OSV_VENDORED_AV

#include "ui/gif_model.h"
#include "ui/tile_thumb.h"
#include "vault/index.h"

#include <vector>

// Tests for GalleryGrid hover animation (Task 10, Phase 47).
//
// The hover animation logic is implemented in GalleryGrid and ImageViewer, which
// cannot be constructed in headless tests. This test file documents the intended
// behavior and covers what can be tested in isolation (the GifHoverGate, which
// drives the hover state). The full integration is tested visually via scripts/run.sh.
//
// The behavior under test:
// - When the cursor dwells on an animated GIF tile for 0.200s, it starts animating
// - Only one hover animation runs at a time across the whole screen
// - Moving off a tile stops its animation immediately
// - Still images never animate on hover
// - Over-budget GIFs (> 1920x1080 or > 300 frames) stay static

namespace {

// Helper: create a gallery node with one animated-GIF child
vault::IndexNode animated_image_node(const char* name)
{
    vault::IndexNode n = vault::IndexNode::image(name);
    n.meta.animated = true;
    n.meta.format = vault::ImageFormat::GIF;
    n.meta.width = 320;
    n.meta.height = 240;
    return n;
}

// Helper: create a gallery node with one still-image child
vault::IndexNode still_image_node(const char* name)
{
    vault::IndexNode n = vault::IndexNode::image(name);
    n.meta.animated = false;
    n.meta.width = 320;
    n.meta.height = 240;
    return n;
}

}  // namespace

TEST(tile_shows_animated_badge_on_animated_image)
{
    auto n = animated_image_node("anim.gif");
    CHECK(ui::tile_shows_animated_badge(n));
}

TEST(tile_shows_animated_badge_not_on_still_image)
{
    auto n = still_image_node("static.jpg");
    CHECK(!ui::tile_shows_animated_badge(n));
}

TEST(tile_shows_animated_badge_not_on_gallery)
{
    vault::IndexNode n = vault::IndexNode::gallery("folder");
    CHECK(!ui::tile_shows_animated_badge(n));
}

TEST(tile_shows_animated_badge_not_on_video)
{
    vault::IndexNode n = vault::IndexNode::video("video.mp4");
    CHECK(!ui::tile_shows_animated_badge(n));
}

TEST(hover_gate_fires_exactly_once_per_tile_dwell)
{
    // This tests GifHoverGate, the state machine that decides when to START
    // the hover animation. The gate fires exactly once per tile when the cursor
    // has dwelt for kGifHoverDwell = 0.200s.
    ui::GifHoverGate gate;

    // Hover on tile 0, but not long enough yet
    CHECK(!gate.update(0, 0.100));
    CHECK_EQ(gate.active_tile(), -1);

    // Add more dwell time; now it fires
    CHECK(gate.update(0, 0.150));  // 0.250s total, past the 0.200s threshold
    CHECK_EQ(gate.active_tile(), 0);

    // Continuing to hover doesn't fire again
    CHECK(!gate.update(0, 0.100));
    CHECK_EQ(gate.active_tile(), 0);
}

TEST(hover_gate_resets_when_cursor_leaves_tile)
{
    // Cursor moves away -> gate resets and stops reporting active tile
    ui::GifHoverGate gate;
    CHECK(gate.update(0, 0.300));  // fire on tile 0
    CHECK_EQ(gate.active_tile(), 0);

    CHECK(!gate.update(-1, 0.016));  // cursor leaves
    CHECK_EQ(gate.active_tile(), -1);
}

TEST(hover_gate_sweeping_across_tiles_fires_nothing)
{
    // Quickly moving the cursor across tiles (less than dwell time on each) never fires
    ui::GifHoverGate gate;
    CHECK(!gate.update(0, 0.050));
    CHECK(!gate.update(1, 0.050));
    CHECK(!gate.update(2, 0.050));
    CHECK_EQ(gate.active_tile(), -1);
}

TEST(gif_within_hover_budget_accepts_small_animated_gif)
{
    // A small animated GIF is within the budget
    CHECK(ui::gif_within_hover_budget(320, 240, 12));
}

TEST(gif_within_hover_budget_accepts_exact_limits)
{
    // The budget is exactly 1920x1080 @ 300 frames
    CHECK(ui::gif_within_hover_budget(1920, 1080, 300));
}

TEST(gif_within_hover_budget_rejects_oversized_dimensions)
{
    // Over-width or over-height GIFs are rejected
    CHECK(!ui::gif_within_hover_budget(1921, 1080, 10));
    CHECK(!ui::gif_within_hover_budget(1920, 1081, 10));
}

TEST(gif_within_hover_budget_rejects_too_many_frames)
{
    // More than 300 frames is over budget
    CHECK(!ui::gif_within_hover_budget(320, 240, 301));
}

#endif  // OSV_VENDORED_AV
