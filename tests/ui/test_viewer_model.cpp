#include "test_framework.h"

#include <cmath>

#include "ui/viewer_model.h"

namespace {
bool approx(float a, float b, float eps = 1e-3f) { return std::abs(a - b) < eps; }
}

TEST(viewer_clamp_zoom_bounds)
{
    CHECK(approx(ui::clamp_zoom(0.0001f), ui::ZOOM_MIN));   // 5% floor
    CHECK(approx(ui::clamp_zoom(9999.0f), ui::ZOOM_MAX));   // 2000% ceiling
    CHECK(approx(ui::clamp_zoom(1.0f), 1.0f));              // in range untouched
    CHECK(approx(ui::ZOOM_MIN, 0.05f));
    CHECK(approx(ui::ZOOM_MAX, 20.0f));
}

TEST(viewer_wrap_index_wraps_at_boundaries)
{
    CHECK_EQ(ui::wrap_index(0, -1, 5), 4);   // first -> last
    CHECK_EQ(ui::wrap_index(4, 1, 5), 0);    // last -> first
    CHECK_EQ(ui::wrap_index(2, 1, 5), 3);    // middle steps normally
    CHECK_EQ(ui::wrap_index(2, -1, 5), 1);
    CHECK_EQ(ui::wrap_index(0, 0, 0), 0);    // empty list is safe
    CHECK_EQ(ui::wrap_index(3, 1, 1), 0);    // single image stays put
}

TEST(viewer_clamp_pan_keeps_image_on_screen)
{
    // Image larger than viewport: pan limited to |sw - vw| / 2 so the image
    // always covers the viewport (never a fully-off-screen drag).
    ui::Vec2 big = ui::clamp_pan({500.0f, -500.0f}, /*sw*/400, /*sh*/400, /*vw*/200, /*vh*/200);
    CHECK(approx(big.x, 100.0f));
    CHECK(approx(big.y, -100.0f));

    // Image smaller than viewport: stays fully inside (same symmetric bound).
    ui::Vec2 small = ui::clamp_pan({80.0f, 0.0f}, /*sw*/100, /*sh*/100, /*vw*/200, /*vh*/200);
    CHECK(approx(small.x, 50.0f));

    // Within bounds: untouched.
    ui::Vec2 ok = ui::clamp_pan({30.0f, -20.0f}, 400, 400, 200, 200);
    CHECK(approx(ok.x, 30.0f));
    CHECK(approx(ok.y, -20.0f));
}

TEST(viewer_strip_scroll_centering)
{
    const float thumb = 100.0f, gap = 10.0f, strip_w = 500.0f;
    const int   count = 10;  // content width = 10*100 + 9*10 = 1090

    // First thumbnail: clamped to 0 (can't scroll before the start).
    CHECK(approx(ui::strip_scroll_centered(0, count, thumb, gap, strip_w), 0.0f));

    // Middle thumbnail: centred. cell_ctr = 5*110 + 50 = 600; scroll = 600 - 250 = 350.
    CHECK(approx(ui::strip_scroll_centered(5, count, thumb, gap, strip_w), 350.0f));

    // Last thumbnail: clamped to content - strip_w = 1090 - 500 = 590.
    CHECK(approx(ui::strip_scroll_centered(9, count, thumb, gap, strip_w), 590.0f));

    // Content narrower than the strip: never scrolls.
    CHECK(approx(ui::strip_scroll_centered(1, 2, thumb, gap, strip_w), 0.0f));
}

TEST(viewer_fit_zoom_uses_smaller_axis)
{
    CHECK(approx(ui::fit_zoom(400, 200, 200, 200), 0.5f));  // width-bound
    CHECK(approx(ui::fit_zoom(200, 400, 200, 200), 0.5f));  // height-bound
    CHECK(approx(ui::fit_zoom(0, 0, 200, 200), 1.0f));      // degenerate
}

TEST(viewer_zoom_at_keeps_cursor_point_fixed)
{
    // Cursor at the viewport centre: zooming keeps pan at zero.
    auto centre = ui::zoom_at(/*img*/{100, 100}, /*zoom*/1.0f, /*pan*/{0, 0},
                              /*factor*/2.0f, /*cursor*/{100, 100}, /*view*/{200, 200});
    CHECK(approx(centre.zoom, 2.0f));
    CHECK(approx(centre.pan.x, 0.0f));
    CHECK(approx(centre.pan.y, 0.0f));

    // Cursor at the right edge of a viewport-sized image that grows past it: the
    // image-space point under the cursor stays under the cursor after zoom.
    auto edge = ui::zoom_at(/*img*/{200, 200}, /*zoom*/1.0f, /*pan*/{0, 0},
                            /*factor*/2.0f, /*cursor*/{200, 100}, /*view*/{200, 200});
    CHECK(approx(edge.zoom, 2.0f));
    CHECK(approx(edge.pan.x, -100.0f));

    // Zoom is clamped: a huge factor cannot exceed ZOOM_MAX.
    auto clamped = ui::zoom_at({100, 100}, 1.0f, {0, 0}, 9999.0f, {100, 100}, {200, 200});
    CHECK(approx(clamped.zoom, ui::ZOOM_MAX));
}

TEST(viewer_edge_nav_hit_left_and_right_zones)
{
    // Viewport [0, 1000): edge = 1000 * 0.12 = 120.
    CHECK_EQ(ui::edge_nav_hit(0.0f, 0.0f, 1000.0f), -1);     // far left edge
    CHECK_EQ(ui::edge_nav_hit(119.0f, 0.0f, 1000.0f), -1);   // just inside left zone
    CHECK_EQ(ui::edge_nav_hit(881.0f, 0.0f, 1000.0f), 1);    // just inside right zone
    CHECK_EQ(ui::edge_nav_hit(999.0f, 0.0f, 1000.0f), 1);    // far right edge
}

TEST(viewer_edge_nav_hit_dead_zone_and_boundaries)
{
    CHECK_EQ(ui::edge_nav_hit(500.0f, 0.0f, 1000.0f), 0);    // dead center
    CHECK_EQ(ui::edge_nav_hit(120.0f, 0.0f, 1000.0f), 0);    // exactly at left boundary: not < edge
    CHECK_EQ(ui::edge_nav_hit(880.0f, 0.0f, 1000.0f), 0);    // exactly at right boundary: not > vp_w - edge
}

TEST(viewer_edge_nav_hit_offset_viewport_and_degenerate_width)
{
    // Viewport offset to start at x=200, width 500: edge = 60.
    CHECK_EQ(ui::edge_nav_hit(210.0f, 200.0f, 500.0f), -1);  // left zone, offset viewport
    CHECK_EQ(ui::edge_nav_hit(690.0f, 200.0f, 500.0f), 1);   // right zone, offset viewport
    CHECK_EQ(ui::edge_nav_hit(450.0f, 200.0f, 500.0f), 0);   // center of offset viewport

    CHECK_EQ(ui::edge_nav_hit(5.0f, 0.0f, 0.0f), 0);         // zero-width viewport: no hit
    CHECK_EQ(ui::edge_nav_hit(5.0f, 0.0f, -10.0f), 0);       // negative width: no hit
}
