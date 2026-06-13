#include "test_framework.h"

#include <SDL3/SDL.h>

#include <algorithm>

#include "gfx/renderer.h"

namespace {
// Bounding box of a point set.
struct Box { float minx, miny, maxx, maxy; };
Box bbox(const std::vector<SDL_FPoint>& pts)
{
    Box b{pts[0].x, pts[0].y, pts[0].x, pts[0].y};
    for (const auto& p : pts) {
        b.minx = std::min(b.minx, p.x); b.miny = std::min(b.miny, p.y);
        b.maxx = std::max(b.maxx, p.x); b.maxy = std::max(b.maxy, p.y);
    }
    return b;
}
} // namespace

TEST(round_rect_outline_touches_all_four_edges)
{
    const SDL_FRect r{10, 20, 200, 120};
    const auto loop = gfx::round_rect_outline(r, 16.0f, 6);
    REQUIRE(!loop.empty());
    const Box b = bbox(loop);
    CHECK_EQ(b.minx, r.x);
    CHECK_EQ(b.miny, r.y);
    CHECK_EQ(b.maxx, r.x + r.w);
    CHECK_EQ(b.maxy, r.y + r.h);
}

TEST(round_rect_outline_stays_within_bounds)
{
    const SDL_FRect r{0, 0, 100, 80};
    const auto loop = gfx::round_rect_outline(r, 20.0f, 8);
    REQUIRE(!loop.empty());
    for (const auto& p : loop) {
        CHECK(p.x >= r.x - 0.001f && p.x <= r.x + r.w + 0.001f);
        CHECK(p.y >= r.y - 0.001f && p.y <= r.y + r.h + 0.001f);
    }
}

TEST(round_rect_outline_clamps_radius_to_half_min_side)
{
    // Huge radius on a square -> a circle inscribed in the square; still bounded.
    const SDL_FRect r{0, 0, 100, 100};
    const auto loop = gfx::round_rect_outline(r, 9999.0f, 12);
    REQUIRE(!loop.empty());
    const Box b = bbox(loop);
    CHECK_EQ(b.minx, 0.0f);
    CHECK_EQ(b.maxx, 100.0f);
    CHECK_EQ(b.miny, 0.0f);
    CHECK_EQ(b.maxy, 100.0f);
}
