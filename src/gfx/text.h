#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "gfx/color.h"

namespace gfx {

// Printable ASCII range baked into the atlas: space (32) .. tilde (126).
inline constexpr int FIRST_GLYPH = 32;
inline constexpr int GLYPH_COUNT = 95;

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
    /// Characters outside the printable-ASCII range are skipped.
    [[nodiscard]] int measure(std::string_view text) const noexcept;

    /// The `y` to pass to draw_text so a single line's rendered ink is vertically
    /// centred on `center_y`. Uses the baked glyphs' real vertical extents, so it
    /// is correct regardless of the nominal pixel height / baseline convention.
    [[nodiscard]] float text_top_for_center(float center_y) const noexcept;

    /// Draw `text` with its top-left at (x, y), tinted `c`. Uploads the atlas
    /// texture lazily on first call. Returns false if not baked or upload fails.
    [[nodiscard]] bool draw_text(SDL_Renderer* r, float x, float y,
                                 std::string_view text, Color c);

    /// Destroy the cached GPU texture (call before the owning renderer is torn
    /// down or recreated). The CPU-side bake is kept; the texture is rebuilt
    /// lazily on the next draw_text().
    void release_texture();

private:
    bool ensure_texture(SDL_Renderer* r);

    bool                    baked_ = false;
    int                     aw_    = 0;
    int                     ah_    = 0;
    float                   px_    = 0.0f;
    std::vector<uint8_t>    bitmap_;            // 8-bit alpha, aw_ * ah_
    std::vector<BakedGlyph> glyphs_;            // GLYPH_COUNT entries when baked
    SDL_Texture*            tex_ = nullptr;     // lazily created from bitmap_
};

} // namespace gfx
