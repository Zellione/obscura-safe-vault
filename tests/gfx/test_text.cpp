#include "test_framework.h"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "gfx/text.h"

namespace {

// The bundled UI font, relative to the repo root (tests run from there).
constexpr const char* kFontPath = OSV_DEFAULT_FONT;

} // namespace

// ---------------------------------------------------------------------------
// Font atlas baking (CPU-only — no SDL needed)
// ---------------------------------------------------------------------------

TEST(font_atlas_bakes_all_printable_ascii)
{
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(kFontPath, 24.0f));
    CHECK(font.baked());
    CHECK(font.atlas_width()  > 0);
    CHECK(font.atlas_height() > 0);
    CHECK_EQ(font.bitmap().size(),
             static_cast<size_t>(font.atlas_width()) * font.atlas_height());

    // Every printable ASCII glyph (space..~) must have advanced the pen.
    for (char c = 32; c < 127; ++c) {
        std::string s(1, c);
        CHECK(font.measure(s) > 0);
    }
}

TEST(font_atlas_bitmap_has_coverage)
{
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(kFontPath, 32.0f));
    // A baked atlas of real glyphs must contain some non-zero alpha coverage.
    bool any_ink = false;
    for (uint8_t px : font.bitmap()) {
        if (px != 0) { any_ink = true; break; }
    }
    CHECK(any_ink);
}

TEST(font_atlas_measure_is_additive)
{
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(kFontPath, 20.0f));
    const int a  = font.measure("a");
    const int b  = font.measure("b");
    const int ab = font.measure("ab");
    CHECK_EQ(ab, a + b);
    CHECK_EQ(font.measure(""), 0);
}

TEST(font_atlas_bake_rejects_garbage)
{
    gfx::FontAtlas font;
    const std::vector<uint8_t> junk(128, 0xAB);
    CHECK_FALSE(font.bake(junk, 16.0f));
    CHECK_FALSE(font.baked());
}

// ---------------------------------------------------------------------------
// draw_text smoke test against a headless software renderer
// ---------------------------------------------------------------------------

TEST(font_atlas_draw_text_runs_headless)
{
    SDL_Surface* surf = SDL_CreateSurface(256, 64, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(surf != nullptr);
    SDL_Renderer* r = SDL_CreateSoftwareRenderer(surf);
    REQUIRE(r != nullptr);

    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);

    // Scope the atlas so its lazily-created texture is destroyed *before* the
    // renderer that owns it.
    {
        gfx::FontAtlas font;
        REQUIRE(font.bake_from_file(kFontPath, 24.0f));
        CHECK(font.draw_text(r, 4.0f, 4.0f, "Obscura", gfx::Color{255, 255, 255, 255}));
        SDL_RenderPresent(r);
    }

    SDL_DestroyRenderer(r);
    SDL_DestroySurface(surf);
}

TEST(text_top_for_center_straddles_center)
{
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(kFontPath, 24.0f));
    const float c = 100.0f;
    const float y = font.text_top_for_center(c);
    // The returned top sits above the centre, and the baseline (y + px_) sits
    // below it, so the rendered ink straddles `c`.
    CHECK(y < c);
    CHECK(y + font.pixel_height() > c);
}
