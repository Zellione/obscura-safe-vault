#include "test_framework.h"

#include "ui/chrome_layout.h"

#include <cmath>

// split_chrome reserves opaque header/footer bands and hands back the content
// area between them. The invariant every test below leans on: the three rects
// tile `area` exactly, top to bottom, with no overlap — that is what guarantees
// a band never covers content and content never scrolls under a band.

namespace {

// header.h + content.h + footer.h == area.h, and each rect starts where the
// previous one ends.
bool tiles_exactly(const SDL_FRect& area, const ui::ChromeBands& b)
{
    constexpr float EPS = 0.001f;
    auto near = [](float a, float c) { return std::abs(a - c) < EPS; };
    return near(b.header.y, area.y) &&
           near(b.header.y + b.header.h, b.content.y) &&
           near(b.content.y + b.content.h, b.footer.y) &&
           near(b.footer.y + b.footer.h, area.y + area.h);
}

} // namespace

TEST(split_chrome_reserves_both_bands)
{
    const SDL_FRect area{0.0f, 0.0f, 1280.0f, 720.0f};
    const auto b = ui::split_chrome(area, 68.0f, 36.0f);

    CHECK_EQ(b.header.h, 68.0f);
    CHECK_EQ(b.footer.h, 36.0f);
    CHECK_EQ(b.content.y, 68.0f);
    CHECK_EQ(b.content.h, 720.0f - 68.0f - 36.0f);
    CHECK_EQ(b.footer.y, 720.0f - 36.0f);
    CHECK(tiles_exactly(area, b));
}

TEST(split_chrome_keeps_x_and_width_of_area)
{
    // The bands span the content column only (e.g. window minus a detail panel),
    // never the full window, so x/w must be carried through untouched.
    const SDL_FRect area{40.0f, 12.0f, 600.0f, 400.0f};
    const auto b = ui::split_chrome(area, 50.0f, 20.0f);

    CHECK_EQ(b.header.x, 40.0f);
    CHECK_EQ(b.content.x, 40.0f);
    CHECK_EQ(b.footer.x, 40.0f);
    CHECK_EQ(b.header.w, 600.0f);
    CHECK_EQ(b.content.w, 600.0f);
    CHECK_EQ(b.footer.w, 600.0f);
    // Bands are offset by area.y, not anchored at 0.
    CHECK_EQ(b.header.y, 12.0f);
    CHECK_EQ(b.content.y, 62.0f);
    CHECK(tiles_exactly(area, b));
}

TEST(split_chrome_zero_bands_give_whole_area_to_content)
{
    // Fullscreen with nothing to report: edge-to-edge picture, no chrome.
    const SDL_FRect area{0.0f, 0.0f, 1920.0f, 1080.0f};
    const auto b = ui::split_chrome(area, 0.0f, 0.0f);

    CHECK_EQ(b.header.h, 0.0f);
    CHECK_EQ(b.footer.h, 0.0f);
    CHECK_EQ(b.content.y, 0.0f);
    CHECK_EQ(b.content.h, 1080.0f);
    CHECK(tiles_exactly(area, b));
}

TEST(split_chrome_footer_only)
{
    // Fullscreen while a status message is showing: footer band forced in, no header.
    const SDL_FRect area{0.0f, 0.0f, 800.0f, 600.0f};
    const auto b = ui::split_chrome(area, 0.0f, 36.0f);

    CHECK_EQ(b.header.h, 0.0f);
    CHECK_EQ(b.content.y, 0.0f);
    CHECK_EQ(b.content.h, 564.0f);
    CHECK_EQ(b.footer.y, 564.0f);
    CHECK(tiles_exactly(area, b));
}

TEST(split_chrome_negative_heights_clamp_to_zero)
{
    const SDL_FRect area{0.0f, 0.0f, 800.0f, 600.0f};
    const auto b = ui::split_chrome(area, -10.0f, -5.0f);

    CHECK_EQ(b.header.h, 0.0f);
    CHECK_EQ(b.footer.h, 0.0f);
    CHECK_EQ(b.content.h, 600.0f);
    CHECK(tiles_exactly(area, b));
}

TEST(split_chrome_bands_too_tall_shrink_proportionally)
{
    // A window shorter than header+footer must not produce overlapping bands or
    // a negative content height.
    const SDL_FRect area{0.0f, 0.0f, 400.0f, 60.0f};
    const auto b = ui::split_chrome(area, 90.0f, 30.0f);

    CHECK_EQ(b.content.h, 0.0f);
    CHECK_EQ(b.header.h, 45.0f);   // 90/120 of 60
    CHECK_EQ(b.footer.h, 15.0f);   // 30/120 of 60
    CHECK_EQ(b.content.y, 45.0f);
    CHECK_EQ(b.footer.y, 45.0f);
    CHECK(tiles_exactly(area, b));
}

TEST(split_chrome_zero_height_area_stays_well_formed)
{
    const SDL_FRect area{0.0f, 0.0f, 400.0f, 0.0f};
    const auto b = ui::split_chrome(area, 68.0f, 36.0f);

    CHECK_EQ(b.header.h, 0.0f);
    CHECK_EQ(b.content.h, 0.0f);
    CHECK_EQ(b.footer.h, 0.0f);
    CHECK(tiles_exactly(area, b));
}

TEST(split_chrome_negative_height_area_stays_well_formed)
{
    // A collapsed window can hand us a negative height; nothing may go inverted.
    const SDL_FRect area{0.0f, 0.0f, 400.0f, -50.0f};
    const auto b = ui::split_chrome(area, 68.0f, 36.0f);

    CHECK(b.header.h >= 0.0f);
    CHECK(b.content.h >= 0.0f);
    CHECK(b.footer.h >= 0.0f);
    CHECK_EQ(b.content.h, 0.0f);
}
