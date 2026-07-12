#include "gfx/text.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "platform/safe_print.h"

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
    aw_ = 0;
    ah_ = 0;
    px_ = 0.0f;
    bitmap_.clear();
    glyphs_.clear();
    if (tex_) { SDL_DestroyTexture(tex_); tex_ = nullptr; }

    if (ttf.empty() || pixel_height <= 0.0f) return false;

    // Validate the font before baking — stb's bake routine has no real error
    // signalling for malformed data, so reject it up front.
    const int offset = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
    if (offset < 0) return false;
    if (stbtt_fontinfo info; !stbtt_InitFont(&info, ttf.data(), offset)) return false;

    // Grow the square atlas until every printable-ASCII glyph fits.
    for (int dim : {256, 512, 1024, 2048}) {
        std::vector<uint8_t>         bmp(static_cast<size_t>(dim) * dim);
        std::vector<stbtt_bakedchar> cd(GLYPH_COUNT);
        if (const int rows = stbtt_BakeFontBitmap(ttf.data(), offset, pixel_height,
                                                  bmp.data(), dim, dim,
                                                  FIRST_GLYPH, GLYPH_COUNT, cd.data());
            rows <= 0)
            continue; // didn't fit — try a larger atlas

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
        platform::safe_println(stderr, "[gfx::FontAtlas] cannot open font '{}'", path);
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

float FontAtlas::text_top_for_center(float center_y) const noexcept
{
    if (!baked_) return center_y - px_ * 0.5f;

    // Real vertical ink extent relative to the baseline, across all glyphs:
    // tops are negative (above baseline), bottoms positive (descenders below).
    float top    = 0.0f;
    float bottom = 0.0f;
    bool  any    = false;
    for (const BakedGlyph& g : glyphs_) {
        if (g.x1 <= g.x0 || g.y1 <= g.y0) continue;  // skip blanks (e.g. space)
        const float gtop = g.yoff;
        const float gbot = g.yoff + static_cast<float>(g.y1 - g.y0);
        if (!any) { top = gtop; bottom = gbot; any = true; }
        else      { top = std::min(top, gtop); bottom = std::max(bottom, gbot); }
    }
    if (!any) return center_y - px_ * 0.5f;

    // draw_text places the baseline at (y + px_); ink centre relative to the
    // baseline is (top + bottom)/2. Solve for the y that centres ink on center_y.
    return center_y - px_ - (top + bottom) * 0.5f;
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

void FontAtlas::build_text_geometry(float x, float y, std::string_view text, Color c,
                                    std::vector<SDL_Vertex>& verts,
                                    std::vector<int>& idx) const
{
    if (!baked_ || aw_ <= 0 || ah_ <= 0) return;

    const SDL_FColor fc{c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
    const float      inv_w = 1.0f / static_cast<float>(aw_);
    const float      inv_h = 1.0f / static_cast<float>(ah_);

    // Caller passes (x, y) as the text's top-left. Approximate the baseline as
    // one pixel-height below the top; glyph yoff (negative) lifts it up.
    float       pen_x    = x;
    const float baseline = y + px_;
    for (unsigned char ch : text) {
        if (ch < FIRST_GLYPH || ch >= FIRST_GLYPH + GLYPH_COUNT) continue;
        const BakedGlyph& g = glyphs_[ch - FIRST_GLYPH];
        if (g.x1 > g.x0 && g.y1 > g.y0) {   // skip zero-area glyphs (e.g. space)
            const auto gw = static_cast<float>(g.x1 - g.x0);
            const auto gh = static_cast<float>(g.y1 - g.y0);
            const float px0 = pen_x + g.xoff;
            const float py0 = baseline + g.yoff;
            const float px1 = px0 + gw;
            const float py1 = py0 + gh;
            const float u0  = static_cast<float>(g.x0) * inv_w;
            const float v0  = static_cast<float>(g.y0) * inv_h;
            const float u1  = static_cast<float>(g.x1) * inv_w;
            const float v1  = static_cast<float>(g.y1) * inv_h;

            const auto base = static_cast<int>(verts.size());
            verts.push_back(SDL_Vertex{{px0, py0}, fc, {u0, v0}});   // TL
            verts.push_back(SDL_Vertex{{px1, py0}, fc, {u1, v0}});   // TR
            verts.push_back(SDL_Vertex{{px1, py1}, fc, {u1, v1}});   // BR
            verts.push_back(SDL_Vertex{{px0, py1}, fc, {u0, v1}});   // BL
            idx.push_back(base + 0);
            idx.push_back(base + 1);
            idx.push_back(base + 2);
            idx.push_back(base + 0);
            idx.push_back(base + 2);
            idx.push_back(base + 3);
        }
        pen_x += g.xadvance;
    }
}

bool FontAtlas::draw_text(SDL_Renderer* r, float x, float y, std::string_view text,
                          Color c)
{
    if (!baked_ || !r) return false;
    if (!ensure_texture(r)) return false;

    // Colour is baked into the per-vertex colour, so no SetTextureColorMod churn.
    verts_.clear();
    idx_.clear();
    build_text_geometry(x, y, text, c, verts_, idx_);
    if (idx_.empty()) return true;   // nothing drawable (e.g. all spaces)

    return SDL_RenderGeometry(r, tex_, verts_.data(), static_cast<int>(verts_.size()),
                              idx_.data(), static_cast<int>(idx_.size()));
}

} // namespace gfx
