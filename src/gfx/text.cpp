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
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include "platform/safe_print.h"

#include "platform/path_utf8.h"

namespace gfx {
namespace {

// Sentinel for "this is not a decodable scalar" — distinct from every real
// codepoint, so callers map it to REPLACEMENT_GLYPH like any unmapped scalar.
constexpr uint32_t BAD_CODEPOINT = 0xFFFFFFFFu;

// Smallest scalar each UTF-8 length may legally encode; anything below is an
// overlong. Indexed by sequence length, so slots 0 and 1 are unused padding.
constexpr std::array<uint32_t, 5> UTF8_MIN_FOR_LEN{0, 0, 0x80, 0x800, 0x10000};

// One byte of `s` as an integer. Goes through std::byte because these bytes are
// UTF-8 machinery rather than characters, and keeps every operand of the bit
// arithmetic below a uint32_t.
[[nodiscard]] constexpr uint32_t byte_at(std::string_view s, size_t i) noexcept
{
    return std::to_integer<uint32_t>(static_cast<std::byte>(s[i]));
}

// Decode one UTF-8 scalar at `i`, advancing `i` past what it consumed.
//
// A vault node name is untrusted input that reaches draw_text directly, so this
// must terminate on ANY byte string: every failure path consumes exactly one
// byte and returns BAD_CODEPOINT, which bounds the loop at one glyph per byte.
// Overlongs, surrogates and out-of-range scalars are rejected rather than
// decoded, so a crafted name cannot alias an ASCII glyph (a '/' smuggled in as
// the overlong C0 AF must not render as '/').
[[nodiscard]] uint32_t next_codepoint(std::string_view s, size_t& i) noexcept
{
    const uint32_t b0 = byte_at(s, i);
    if (b0 < 0x80) { ++i; return b0; }

    size_t   len = 0;
    uint32_t cp  = 0;
    if      ((b0 & 0xE0) == 0xC0) { len = 2; cp = b0 & 0x1Fu; }
    else if ((b0 & 0xF0) == 0xE0) { len = 3; cp = b0 & 0x0Fu; }
    else if ((b0 & 0xF8) == 0xF0) { len = 4; cp = b0 & 0x07u; }
    else                          { ++i; return BAD_CODEPOINT; }

    if (i + len > s.size()) { ++i; return BAD_CODEPOINT; }
    for (size_t k = 1; k < len; ++k) {
        const uint32_t bk = byte_at(s, i + k);
        if ((bk & 0xC0) != 0x80) { ++i; return BAD_CODEPOINT; }
        cp = (cp << 6) | (bk & 0x3Fu);
    }

    if (cp < UTF8_MIN_FOR_LEN[len] || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
        ++i;                       // overlong / surrogate / out of range
        return BAD_CODEPOINT;
    }
    i += len;
    return cp;
}

} // namespace

const BakedGlyph* FontAtlas::exact_glyph_for(uint32_t cp) const noexcept
{
    if (!baked_) return nullptr;
    if (cp >= FIRST_GLYPH && cp < FIRST_GLYPH + static_cast<uint32_t>(GLYPH_COUNT))
        return &glyphs_[cp - FIRST_GLYPH];
    const auto it = std::ranges::lower_bound(extra_, cp, {}, &SparseGlyph::cp);
    return (it != extra_.end() && it->cp == cp) ? &it->glyph : nullptr;
}

const BakedGlyph* FontAtlas::glyph_for(uint32_t cp) const noexcept
{
    if (const BakedGlyph* g = exact_glyph_for(cp)) return g;
    return exact_glyph_for(REPLACEMENT_GLYPH);
}

bool FontAtlas::has_glyph_for(std::string_view text) const noexcept
{
    if (!baked_) return false;
    for (size_t i = 0; i < text.size();) {
        const uint32_t cp = next_codepoint(text, i);
        if (cp == BAD_CODEPOINT || !exact_glyph_for(cp)) return false;
    }
    return true;
}

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
    extra_.clear();
    if (tex_) { SDL_DestroyTexture(tex_); tex_ = nullptr; }

    if (ttf.empty() || pixel_height <= 0.0f) return false;

    // Validate the font before baking — stb's bake routines have no real error
    // signalling for malformed data, so reject it up front.
    const int offset = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
    if (offset < 0) return false;
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf.data(), offset)) return false;

    // ASCII first (dense, index 0..GLYPH_COUNT-1), then every EXTRA_RANGES
    // codepoint the font actually maps. Filtering against the cmap here is what
    // lets EXTRA_RANGES name whole blocks without caring which subset of the
    // bundled font is present: an unmapped codepoint would otherwise pack a
    // blank .notdef rect and report a bogus advance.
    std::vector<int> cps;
    cps.reserve(GLYPH_COUNT + 256);
    for (int cp = FIRST_GLYPH; cp < FIRST_GLYPH + GLYPH_COUNT; ++cp) cps.push_back(cp);
    const size_t ascii_end = cps.size();
    for (const CodepointRange& r : EXTRA_RANGES) {
        for (uint32_t cp = r.first; cp <= r.last; ++cp) {
            if (stbtt_FindGlyphIndex(&info, static_cast<int>(cp)) != 0)
                cps.push_back(static_cast<int>(cp));
        }
    }

    // Grow the square atlas until every glyph fits.
    for (int dim : {256, 512, 1024, 2048}) {
        std::vector<uint8_t>          bmp(static_cast<size_t>(dim) * dim);
        std::vector<stbtt_packedchar> cd(cps.size());

        stbtt_pack_context pc;
        if (!stbtt_PackBegin(&pc, bmp.data(), dim, dim, 0, /*padding*/ 1, nullptr)) continue;
        stbtt_pack_range range{};
        range.font_size                        = pixel_height;
        range.first_unicode_codepoint_in_range = 0;
        range.array_of_unicode_codepoints      = cps.data();
        range.num_chars                        = static_cast<int>(cps.size());
        range.chardata_for_range               = cd.data();
        const int ok = stbtt_PackFontRanges(&pc, ttf.data(), 0, &range, 1);
        stbtt_PackEnd(&pc);
        if (!ok) continue;   // didn't fit — try a larger atlas

        // Oversampling stays 1x1 (stb's default here), so packedchar's xoff /
        // yoff / xadvance carry exactly the bakedchar semantics this class was
        // written against: offsets from the pen, baseline-relative y.
        const auto to_glyph = [&cd](size_t i) {
            return BakedGlyph{
                .x0 = cd[i].x0, .y0 = cd[i].y0, .x1 = cd[i].x1, .y1 = cd[i].y1,
                .xoff = cd[i].xoff, .yoff = cd[i].yoff, .xadvance = cd[i].xadvance};
        };

        aw_ = dim;
        ah_ = dim;
        px_ = pixel_height;
        bitmap_ = std::move(bmp);
        glyphs_.resize(GLYPH_COUNT);
        for (size_t i = 0; i < ascii_end; ++i) glyphs_[i] = to_glyph(i);
        extra_.reserve(cps.size() - ascii_end);
        for (size_t i = ascii_end; i < cps.size(); ++i)
            extra_.emplace_back(static_cast<uint32_t>(cps[i]), to_glyph(i));
        // EXTRA_RANGES is authored in ascending order, but sorting makes
        // glyph_for's binary search correct regardless of how it is edited.
        std::ranges::sort(extra_, {}, &SparseGlyph::cp);
        baked_ = true;
        return true;
    }
    return false;
}

bool FontAtlas::bake_from_file(const char* path, float pixel_height)
{
    if (!path) return false;
    std::FILE* f = platform::fopen_path(platform::utf8_to_path(path), "rb");
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
    for (size_t i = 0; i < text.size();) {
        if (const BakedGlyph* g = glyph_for(next_codepoint(text, i)))
            width += static_cast<int>(std::lround(g->xadvance));
    }
    return width;
}

float FontAtlas::text_top_for_center(float center_y) const noexcept
{
    if (!baked_) return center_y - px_ * 0.5f;

    // ASCII only, deliberately: extra_ holds accented capitals and cedillas
    // whose ink reaches further up and down than any ASCII glyph, so folding
    // them in would move the vertical centring of every existing screen the
    // moment coverage grew. The basis stays what every layout was tuned against.
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
    for (size_t i = 0; i < text.size();) {
        const BakedGlyph* gp = glyph_for(next_codepoint(text, i));
        if (!gp) continue;
        const BakedGlyph& g = *gp;
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
