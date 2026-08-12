#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "gfx/color.h"

namespace gfx {

// Printable ASCII, baked as a dense array and looked up by subtraction: this is
// the overwhelming majority of every string the UI draws, so it skips the
// sparse table below entirely.
inline constexpr int FIRST_GLYPH = 32;
inline constexpr int GLYPH_COUNT = 95;

// Codepoint drawn in place of any scalar the bundled font cannot render, and
// of any malformed UTF-8 byte. Dropping such text instead (the pre-Phase-83
// behaviour) makes two differently-named files render identically blank.
inline constexpr uint32_t REPLACEMENT_GLYPH = '?';

// Contiguous codepoint blocks the atlas attempts, beyond ASCII. Each is
// filtered against the font's own cmap at bake time, so listing a block the
// bundled font lacks costs nothing. Latin-1 + Latin Extended-A cover Western
// and Central European node names; General Punctuation carries the typography
// the UI writes (— – … • ‖); U+2212 is the MINUS SIGN.
struct CodepointRange {
    uint32_t first;
    uint32_t last;
};
inline constexpr CodepointRange EXTRA_RANGES[] = {
    {0x00A0, 0x00FF},   // Latin-1 Supplement (· × ± § µ and accented letters)
    {0x0100, 0x017F},   // Latin Extended-A
    {0x0180, 0x024F},   // Latin Extended-B
    {0x0370, 0x03FF},   // Greek
    {0x0400, 0x04FF},   // Cyrillic
    {0x2010, 0x2027},   // General Punctuation (– — … • ‖ quotes)
    {0x20A0, 0x20BF},   // Currency symbols
    {0x2212, 0x2212},   // MINUS SIGN
};

/// One baked glyph: its rectangle within the atlas bitmap plus placement metrics.
struct BakedGlyph {
    // Atlas pixel rect [x0,x1) x [y0,y1).
    uint16_t x0 = 0;
    uint16_t y0 = 0;
    uint16_t x1 = 0;
    uint16_t y1 = 0;
    // Offset from the pen to the glyph's top-left.
    float xoff = 0;
    float yoff = 0;
    // Pen advance after this glyph.
    float xadvance = 0;
};

/// A baked glyph atlas for printable ASCII, rendered with stb_truetype.
///
/// `bake()` is pure CPU work (an 8-bit alpha coverage bitmap + per-glyph metrics)
/// and needs no SDL — so it is unit-testable headlessly. The GPU `SDL_Texture` is
/// created lazily on the first `draw_text()` call and owned by the atlas.
class FontAtlas {
public:
    FontAtlas() = default;
    ~FontAtlas();

    FontAtlas(const FontAtlas&)            = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;
    FontAtlas(FontAtlas&&)                 = delete;
    FontAtlas& operator=(FontAtlas&&)      = delete;

    /// Bake glyphs from an in-memory TTF/OTF buffer at the given pixel height.
    /// Returns false on invalid font data or if the atlas overflows.
    [[nodiscard]] bool bake(std::span<const uint8_t> ttf, float pixel_height);

    /// Read a font file from disk, then bake(). Returns false if the file is
    /// missing/unreadable or the data is not a valid font.
    [[nodiscard]] bool bake_from_file(const char* path, float pixel_height);

    [[nodiscard]] bool  baked()        const noexcept { return baked_; }
    [[nodiscard]] int   atlas_width()  const noexcept { return aw_; }
    [[nodiscard]] int   atlas_height() const noexcept { return ah_; }
    [[nodiscard]] float pixel_height() const noexcept { return px_; }

    /// 8-bit alpha coverage bitmap of the atlas (atlas_width * atlas_height bytes).
    [[nodiscard]] std::span<const uint8_t> bitmap() const noexcept { return bitmap_; }

    /// Pixel width of `text` if rendered (sum of advances; no kerning).
    /// `text` is decoded as UTF-8; a scalar with no baked glyph, and each byte
    /// of a malformed sequence, measures as REPLACEMENT_GLYPH.
    [[nodiscard]] int measure(std::string_view text) const noexcept;

    /// Whether every UTF-8 scalar in `text` has a glyph of its own — i.e. none
    /// of it would fall back to REPLACEMENT_GLYPH. Malformed input is false.
    [[nodiscard]] bool has_glyph_for(std::string_view text) const noexcept;

    /// The `y` to pass to draw_text so a single line's rendered ink is vertically
    /// centred on `center_y`. Uses the baked glyphs' real vertical extents, so it
    /// is correct regardless of the nominal pixel height / baseline convention.
    [[nodiscard]] float text_top_for_center(float center_y) const noexcept;

    /// Draw `text` with its top-left at (x, y), tinted `c`. Uploads the atlas
    /// texture lazily on first call. Returns false if not baked or upload fails.
    /// Issues a single SDL_RenderGeometry call for the whole run (one textured
    /// quad per drawable glyph) rather than one draw call per glyph.
    [[nodiscard]] bool draw_text(SDL_Renderer* r, float x, float y,
                                 std::string_view text, Color c);

    /// Append the textured-quad geometry for `text` placed with its top-left at
    /// (x, y), tinted `c`, into `verts`/`idx`: 4 vertices + 6 indices per
    /// drawable glyph (zero-area glyphs like space are skipped). Texture coords
    /// are normalised against the atlas size; indices are offset by the current
    /// `verts.size()` so callers may batch several runs into one buffer pair.
    /// Pure CPU work (no SDL renderer) — the layout/UV maths is unit-testable
    /// headlessly. Does nothing if the atlas is not baked.
    void build_text_geometry(float x, float y, std::string_view text, Color c,
                             std::vector<SDL_Vertex>& verts,
                             std::vector<int>& idx) const;

    /// Destroy the cached GPU texture (call before the owning renderer is torn
    /// down or recreated). The CPU-side bake is kept; the texture is rebuilt
    /// lazily on the next draw_text().
    void release_texture();

private:
    /// One baked non-ASCII glyph, keyed by its codepoint.
    struct SparseGlyph {
        uint32_t   cp = 0;
        BakedGlyph glyph;
    };

    bool ensure_texture(SDL_Renderer* r);

    /// Glyph for `cp`, falling back to REPLACEMENT_GLYPH's when it has none.
    /// Never null once baked.
    [[nodiscard]] const BakedGlyph* glyph_for(uint32_t cp) const noexcept;
    /// Glyph baked specifically for `cp`, or nullptr — no fallback.
    [[nodiscard]] const BakedGlyph* exact_glyph_for(uint32_t cp) const noexcept;

    bool                    baked_ = false;
    int                     aw_    = 0;
    int                     ah_    = 0;
    float                   px_    = 0.0f;
    std::vector<uint8_t>    bitmap_;            // 8-bit alpha, aw_ * ah_
    std::vector<BakedGlyph> glyphs_;            // GLYPH_COUNT entries when baked
    // Non-ASCII glyphs, sorted by codepoint for binary search. Kept out of
    // glyphs_ so the ASCII path stays a subtraction, and out of
    // text_top_for_center's ink extent so extended coverage cannot shift the
    // vertical centring of every existing screen.
    std::vector<SparseGlyph> extra_;
    SDL_Texture*            tex_ = nullptr;     // lazily created from bitmap_

    // Reused per draw_text() to batch a run's glyph quads into one geometry
    // submission without per-call heap churn (cleared, not reallocated).
    mutable std::vector<SDL_Vertex> verts_;
    mutable std::vector<int>        idx_;
};

} // namespace gfx
