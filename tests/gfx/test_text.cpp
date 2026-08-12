#include "test_framework.h"

#include <SDL3/SDL.h>

#include <algorithm>
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

// ---------------------------------------------------------------------------
// Batched glyph geometry (CPU-only — no SDL renderer needed)
// ---------------------------------------------------------------------------

TEST(build_text_geometry_emits_quad_per_drawable_glyph)
{
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(kFontPath, 24.0f));

    std::vector<SDL_Vertex> verts;
    std::vector<int>        idx;
    // "AB" — two drawable glyphs, no spaces.
    font.build_text_geometry(0.0f, 0.0f, "AB", gfx::Color{255, 255, 255, 255}, verts, idx);
    CHECK_EQ(verts.size(), size_t{8});   // 4 vertices per glyph
    CHECK_EQ(idx.size(),   size_t{12});  // 6 indices  per glyph

    // Every index must reference a valid vertex.
    for (int i : idx) CHECK(i >= 0 && i < static_cast<int>(verts.size()));

    // Texture coords are normalised into the atlas.
    for (const auto& v : verts) {
        CHECK(v.tex_coord.x >= 0.0f && v.tex_coord.x <= 1.0f);
        CHECK(v.tex_coord.y >= 0.0f && v.tex_coord.y <= 1.0f);
    }
}

TEST(build_text_geometry_skips_spaces)
{
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(kFontPath, 24.0f));

    std::vector<SDL_Vertex> verts;
    std::vector<int>        idx;
    // Three spaces are zero-area glyphs and contribute no geometry.
    font.build_text_geometry(0.0f, 0.0f, "   ", gfx::Color{255, 255, 255, 255}, verts, idx);
    CHECK_EQ(verts.size(), size_t{0});
    CHECK_EQ(idx.size(),   size_t{0});
}

TEST(build_text_geometry_offsets_indices_when_batching)
{
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(kFontPath, 24.0f));

    std::vector<SDL_Vertex> verts;
    std::vector<int>        idx;
    font.build_text_geometry(0.0f, 0.0f, "A", gfx::Color{255, 255, 255, 255}, verts, idx);
    const size_t after_first = idx.size();
    // Appending a second run must reference the newly added vertices (offset by
    // the prior vertex count), never re-index the first run's vertices.
    font.build_text_geometry(50.0f, 0.0f, "B", gfx::Color{255, 255, 255, 255}, verts, idx);
    CHECK_EQ(verts.size(), size_t{8});
    CHECK_EQ(idx.size(),   size_t{12});
    int min_second = static_cast<int>(verts.size());
    for (size_t i = after_first; i < idx.size(); ++i) min_second = std::min(min_second, idx[i]);
    CHECK(min_second >= 4);   // second run indices point past the first run's 4 verts
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

// ---------------------------------------------------------------------------
// Phase 83 — non-ASCII coverage
//
// The atlas used to bake only bytes 32..126 and SILENTLY SKIP everything else,
// in both measure() and build_text_geometry(). Every UTF-8 sequence in the UI
// therefore rendered as nothing: the F2 settings footer's "[↑↓] Move  [←→]
// Change" showed as "[] Move  [] Change", and an accented filename lost its
// accented letters outright.
// ---------------------------------------------------------------------------

TEST(font_atlas_renders_ui_typography)
{
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(kFontPath, 24.0f));

    // Every non-ASCII codepoint the UI actually puts on screen. If a new one is
    // introduced that the bundled Noto Sans subset cannot draw, add it here and
    // this test says so before a user reports an invisible character.
    for (const char* s : {"—", "–", "…", "·", "•", "−", "×", "±", "§", "µ", "‖"}) {
        CHECK(font.measure(s) > 0);
        CHECK(font.has_glyph_for(s));
    }
}

TEST(font_atlas_renders_accented_names)
{
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(kFontPath, 24.0f));
    // Vault node names are arbitrary user text; Latin-1 and Latin Extended-A
    // cover Western and Central European filenames.
    for (const char* s : {"é", "ü", "ñ", "å", "ç", "ø", "ł", "ř", "ő"}) {
        CHECK(font.measure(s) > 0);
        CHECK(font.has_glyph_for(s));
    }
    // "café.jpg" must be wider than "caf.jpg" — the accented letter is drawn,
    // not dropped.
    CHECK(font.measure("café.jpg") > font.measure("caf.jpg"));

    // Greek and Cyrillic are in the bundled subset too, so names in those
    // scripts render rather than degrading to a row of '?'.
    for (const char* s : {"α", "Ω", "д", "Я"}) {
        CHECK(font.measure(s) > 0);
        CHECK(font.has_glyph_for(s));
    }
}

TEST(font_atlas_measure_counts_utf8_scalars_not_bytes)
{
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(kFontPath, 24.0f));
    // A 3-byte sequence advances the pen once, not three times, and measure()
    // stays exactly additive across the mixed run.
    CHECK_EQ(font.measure("a—b"),
             font.measure("a") + font.measure("—") + font.measure("b"));
    CHECK_EQ(font.measure("——"), 2 * font.measure("—"));
}

TEST(font_atlas_unmapped_codepoint_draws_a_replacement)
{
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(kFontPath, 24.0f));
    // The bundled font has no CJK. Silently dropping such a name would render
    // two different files identically blank, so an unmapped scalar falls back
    // to one '?' — one per scalar, not one per UTF-8 byte.
    CHECK_FALSE(font.has_glyph_for("あ"));
    CHECK_EQ(font.measure("あ"), font.measure("?"));
    CHECK_EQ(font.measure("あい"), font.measure("??"));
}

TEST(font_atlas_malformed_utf8_terminates)
{
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(kFontPath, 24.0f));
    // A vault name is untrusted bytes: a lone continuation byte, a truncated
    // sequence and an overlong encoding must each consume input and render a
    // replacement rather than hang or alias an ASCII glyph.
    CHECK_EQ(font.measure("\x80"),         font.measure("?"));
    CHECK_EQ(font.measure("\xE2\x80"),     font.measure("??"));
    CHECK_EQ(font.measure("\xC0\xAF"),     font.measure("??"));   // overlong '/'
    CHECK_EQ(font.measure("a\xFF" "b"),    font.measure("a?b"));
}

TEST(build_text_geometry_emits_quad_for_non_ascii)
{
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(kFontPath, 24.0f));

    std::vector<SDL_Vertex> verts;
    std::vector<int>        idx;
    font.build_text_geometry(0.0f, 0.0f, "—", gfx::Color{255, 255, 255, 255}, verts, idx);
    CHECK_EQ(verts.size(), size_t{4});
    CHECK_EQ(idx.size(),   size_t{6});
    for (const auto& v : verts) {
        CHECK(v.tex_coord.x >= 0.0f && v.tex_coord.x <= 1.0f);
        CHECK(v.tex_coord.y >= 0.0f && v.tex_coord.y <= 1.0f);
    }
}

TEST(font_atlas_ascii_metrics_unchanged_by_extended_coverage)
{
    // Adding codepoints must not move existing ASCII layout: the vertical
    // centring basis stays the ASCII ink extent, so no screen shifts.
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(kFontPath, 24.0f));
    CHECK(font.measure("Obscura") > 0);
    const float y = font.text_top_for_center(100.0f);
    CHECK(y < 100.0f);
    CHECK(y + font.pixel_height() > 100.0f);
}


