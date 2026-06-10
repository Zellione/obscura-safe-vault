#include "gfx/text.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#pragma GCC diagnostic pop

#include <cmath>
#include <cstdio>

namespace gfx {

FontAtlas::~FontAtlas()
{
    release_texture();
}

void FontAtlas::release_texture()
{
    if (tex_) {
        SDL_DestroyTexture(tex_);
        tex_ = nullptr;
    }
}

bool FontAtlas::bake(std::span<const uint8_t> ttf, float pixel_height)
{
    // Reset any previous bake.
    baked_ = false;
    aw_ = ah_ = 0;
    px_ = 0.0f;
    bitmap_.clear();
    glyphs_.clear();
    if (tex_) { SDL_DestroyTexture(tex_); tex_ = nullptr; }

    if (ttf.empty() || pixel_height <= 0.0f) return false;

    // Validate the font before baking — stb's bake routine has no real error
    // signalling for malformed data, so reject it up front.
    const int offset = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
    if (offset < 0) return false;
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf.data(), offset)) return false;

    // Grow the square atlas until every printable-ASCII glyph fits.
    for (int dim : {256, 512, 1024, 2048}) {
        std::vector<uint8_t>         bmp(static_cast<size_t>(dim) * dim);
        std::vector<stbtt_bakedchar> cd(GLYPH_COUNT);
        const int rows = stbtt_BakeFontBitmap(ttf.data(), offset, pixel_height,
                                              bmp.data(), dim, dim,
                                              FIRST_GLYPH, GLYPH_COUNT, cd.data());
        if (rows <= 0) continue; // didn't fit — try a larger atlas

        aw_ = dim;
        ah_ = dim;
        px_ = pixel_height;
        bitmap_ = std::move(bmp);
        glyphs_.resize(GLYPH_COUNT);
        for (int i = 0; i < GLYPH_COUNT; ++i) {
            glyphs_[i] = BakedGlyph{
                .x0 = cd[i].x0, .y0 = cd[i].y0, .x1 = cd[i].x1, .y1 = cd[i].y1,
                .xoff = cd[i].xoff, .yoff = cd[i].yoff, .xadvance = cd[i].xadvance};
        }
        baked_ = true;
        return true;
    }
    return false;
}

bool FontAtlas::bake_from_file(const char* path, float pixel_height)
{
    if (!path) return false;
    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "[gfx::FontAtlas] cannot open font '%s'\n", path);
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0) { std::fclose(f); return false; }

    std::vector<uint8_t> data(static_cast<size_t>(size));
    const size_t got = std::fread(data.data(), 1, data.size(), f);
    std::fclose(f);
    if (got != data.size()) return false;

    return bake(data, pixel_height);
}

int FontAtlas::measure(std::string_view text) const noexcept
{
    if (!baked_) return 0;
    // Round each glyph advance independently so measure() is exactly additive:
    // measure("ab") == measure("a") + measure("b").
    int width = 0;
    for (unsigned char ch : text) {
        if (ch < FIRST_GLYPH || ch >= FIRST_GLYPH + GLYPH_COUNT) continue;
        width += static_cast<int>(std::lround(glyphs_[ch - FIRST_GLYPH].xadvance));
    }
    return width;
}

bool FontAtlas::ensure_texture(SDL_Renderer* r)
{
    if (tex_) return true;
    if (!baked_ || !r) return false;

    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_STATIC, aw_, ah_);
    if (!t) return false;

    // Expand the 8-bit alpha coverage into white RGBA pixels (R=G=B=255,
    // A=coverage). Byte order R,G,B,A is what SDL_PIXELFORMAT_RGBA32 expects in
    // memory regardless of host endianness.
    std::vector<uint8_t> rgba(static_cast<size_t>(aw_) * ah_ * 4);
    for (size_t i = 0; i < bitmap_.size(); ++i) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = bitmap_[i];
    }
    if (!SDL_UpdateTexture(t, nullptr, rgba.data(), aw_ * 4)) {
        SDL_DestroyTexture(t);
        return false;
    }
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    tex_ = t;
    return true;
}

bool FontAtlas::draw_text(SDL_Renderer* r, float x, float y, std::string_view text,
                          Color c)
{
    if (!baked_ || !r) return false;
    if (!ensure_texture(r)) return false;

    SDL_SetTextureColorMod(tex_, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(tex_, c.a);

    // Caller passes (x, y) as the text's top-left. Approximate the baseline as
    // one pixel-height below the top; glyph yoff (negative) lifts it up.
    float       pen_x    = x;
    const float baseline = y + px_;
    for (unsigned char ch : text) {
        if (ch < FIRST_GLYPH || ch >= FIRST_GLYPH + GLYPH_COUNT) continue;
        const BakedGlyph& g = glyphs_[ch - FIRST_GLYPH];
        const float gw = static_cast<float>(g.x1 - g.x0);
        const float gh = static_cast<float>(g.y1 - g.y0);
        if (gw > 0.0f && gh > 0.0f) {
            const SDL_FRect src{static_cast<float>(g.x0), static_cast<float>(g.y0),
                                gw, gh};
            const SDL_FRect dst{pen_x + g.xoff, baseline + g.yoff, gw, gh};
            SDL_RenderTexture(r, tex_, &src, &dst);
        }
        pen_x += g.xadvance;
    }
    return true;
}

} // namespace gfx
