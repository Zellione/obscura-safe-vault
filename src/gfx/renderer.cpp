#include "gfx/renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace gfx {

void Renderer::draw_rect(const SDL_FRect& dst, Color c, bool filled)
{
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    if (filled)
        SDL_RenderFillRect(r_, &dst);
    else
        SDL_RenderRect(r_, &dst);
}

void Renderer::draw_image(SDL_Texture* tex, const SDL_FRect& dst, Color tint)
{
    if (!tex) return;
    SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(tex, tint.a);
    SDL_RenderTexture(r_, tex, nullptr, &dst);
}

void Renderer::draw_image(SDL_Texture* tex, const SDL_FRect& src,
                          const SDL_FRect& dst, Color tint)
{
    if (!tex) return;
    SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(tex, tint.a);
    SDL_RenderTexture(r_, tex, &src, &dst);
}

void Renderer::draw_text(FontAtlas& font, float x, float y, std::string_view text,
                         Color c)
{
    (void)font.draw_text(r_, x, y, text, c);
}

float Renderer::draw_thumbnail_strip(std::span<SDL_Texture* const> thumbs,
                                     const SDL_FRect& strip, const ThumbnailStrip& opts)
{
    const float thumb_size = opts.size;
    SDL_Rect clip{static_cast<int>(strip.x), static_cast<int>(strip.y),
                  static_cast<int>(strip.w), static_cast<int>(strip.h)};
    SDL_SetRenderClipRect(r_, &clip);

    // Lay cells out along the strip's long axis; centre them on the short axis.
    const float cross  = opts.vertical ? strip.x + (strip.w - thumb_size) * 0.5f
                                        : strip.y + (strip.h - thumb_size) * 0.5f;
    const float origin = opts.vertical ? strip.y : strip.x;
    const float extent = opts.vertical ? strip.h : strip.w;

    for (int i = 0; i < static_cast<int>(thumbs.size()); ++i) {
        const float along = origin - opts.scroll +
                            static_cast<float>(i) * (thumb_size + opts.gap);
        if (along + thumb_size < origin || along > origin + extent) continue;  // culled

        const SDL_FRect dst = opts.vertical
            ? SDL_FRect{cross, along, thumb_size, thumb_size}
            : SDL_FRect{along, cross, thumb_size, thumb_size};

        // Black backing so the letterbox bands around a non-square thumbnail
        // read as solid black rather than the strip colour.
        SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
        SDL_RenderFillRect(r_, &dst);

        if (thumbs[i]) {
            // Aspect-fit into the square cell: largest size that keeps the
            // thumbnail's own proportions, centred (no stretching).
            float tw = 0.0f;
            float th = 0.0f;
            SDL_GetTextureSize(thumbs[i], &tw, &th);
            SDL_FRect img = dst;
            if (tw > 0.0f && th > 0.0f) {
                const float scale = std::min(dst.w / tw, dst.h / th);
                const float w = tw * scale;
                const float h = th * scale;
                img = SDL_FRect{dst.x + (dst.w - w) * 0.5f,
                                dst.y + (dst.h - h) * 0.5f, w, h};
            }
            SDL_SetTextureColorMod(thumbs[i], 255, 255, 255);
            SDL_SetTextureAlphaMod(thumbs[i], 255);
            SDL_RenderTexture(r_, thumbs[i], nullptr, &img);
        }
        if (i == opts.selected) draw_selection_glow(dst, 6.0f, opts.highlight);
    }

    SDL_SetRenderClipRect(r_, nullptr);
    return thumbnail_strip_content_width(static_cast<int>(thumbs.size()),
                                         thumb_size, opts.gap);
}

// Fill `out` with the rounded-rectangle outline loop (see round_rect_outline).
// The per-point angle trig depends only on `segments` — not on `dst`/`radius` —
// so it is computed once and cached per segment count, leaving the hot path to
// just scale + translate. `out` is cleared and reused (no per-call allocation).
static void round_rect_outline_into(std::vector<SDL_FPoint>& out, const SDL_FRect& dst,
                                    float radius, int segments)
{
    constexpr float pi = std::numbers::pi_v<float>;
    const float r = std::max(0.0f, std::min(radius, std::min(dst.w, dst.h) * 0.5f));
    if (segments < 1) segments = 1;

    // Cached unit-circle samples for the four quarter arcs (TL, TR, BR, BL),
    // swept clockwise. In screen coords (+y down) cos/sin still trace a circle,
    // so the loop touches all four edges -> its bbox equals `dst`.
    struct ArcCache {
        int                segments = -1;
        std::vector<float> cosv;
        std::vector<float> sinv;
    };
    static thread_local ArcCache ac;
    if (ac.segments != segments) {
        ac.segments = segments;
        ac.cosv.clear();
        ac.sinv.clear();
        const float quarter = pi * 0.5f;
        const std::array<float, 4> a0{pi, pi * 1.5f, 0.0f, pi * 0.5f};
        for (const float start : a0)
            for (int s = 0; s <= segments; ++s) {
                const float a = start + quarter * (static_cast<float>(s) /
                                                   static_cast<float>(segments));
                ac.cosv.push_back(std::cos(a));
                ac.sinv.push_back(std::sin(a));
            }
    }

    struct Corner { float cx; float cy; };
    const std::array<Corner, 4> corners{{
        {dst.x + r,         dst.y + r},          // TL
        {dst.x + dst.w - r, dst.y + r},          // TR
        {dst.x + dst.w - r, dst.y + dst.h - r},  // BR
        {dst.x + r,         dst.y + dst.h - r},  // BL
    }};

    const int per = segments + 1;
    out.clear();
    out.reserve(static_cast<size_t>(4 * per));
    for (int k = 0; k < 4 * per; ++k) {
        const Corner& c = corners[static_cast<size_t>(k / per)];
        out.push_back(SDL_FPoint{c.cx + r * ac.cosv[static_cast<size_t>(k)],
                                 c.cy + r * ac.sinv[static_cast<size_t>(k)]});
    }
}

std::vector<SDL_FPoint> round_rect_outline(const SDL_FRect& dst, float radius,
                                           int segments)
{
    std::vector<SDL_FPoint> out;
    round_rect_outline_into(out, dst, radius, segments);
    return out;
}

void Renderer::draw_round_rect(const SDL_FRect& dst, float radius, Color c, bool filled)
{
    if (radius <= 0.0f) { draw_rect(dst, c, filled); return; }

    // Reused across calls (single-threaded render loop) to avoid per-call heap
    // churn — a gallery frame issues dozens of rounded rects.
    static thread_local std::vector<SDL_FPoint> loop;
    round_rect_outline_into(loop, dst, radius, 6);
    if (loop.empty()) return;

    if (!filled) {
        SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
        static thread_local std::vector<SDL_FPoint> closed;
        closed.assign(loop.begin(), loop.end());
        closed.push_back(loop.front());          // close the loop
        SDL_RenderLines(r_, closed.data(), static_cast<int>(closed.size()));
        return;
    }

    // Triangle fan from the rect centre over the outline loop.
    const SDL_FColor fc{c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
    const SDL_FPoint centre{dst.x + dst.w * 0.5f, dst.y + dst.h * 0.5f};
    const auto n = static_cast<int>(loop.size());

    static thread_local std::vector<SDL_Vertex> verts;
    verts.clear();
    verts.reserve(loop.size() + 1);
    verts.push_back(SDL_Vertex{centre, fc, {0, 0}});
    for (const auto& p : loop) verts.push_back(SDL_Vertex{p, fc, {0, 0}});

    // The index pattern depends only on `n` (constant per segment count); rebuild
    // only when that changes.
    static thread_local std::vector<int> idx;
    if (static_cast<int>(idx.size()) != n * 3) {
        idx.clear();
        idx.reserve(static_cast<size_t>(n) * 3);
        for (int i = 0; i < n; ++i) {
            idx.push_back(0);
            idx.push_back(1 + i);
            idx.push_back(1 + (i + 1) % n);
        }
    }
    SDL_RenderGeometry(r_, nullptr, verts.data(), static_cast<int>(verts.size()),
                       idx.data(), static_cast<int>(idx.size()));
}

void Renderer::draw_selection_glow(const SDL_FRect& dst, float radius, Color accent)
{
    // A few progressively larger, fainter rounded outlines just outside `dst`.
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    constexpr int rings = 3;
    for (int i = 0; i < rings; ++i) {
        const float grow = static_cast<float>(i + 1) * 2.0f;
        const SDL_FRect rr{dst.x - grow, dst.y - grow,
                           dst.w + 2.0f * grow, dst.h + 2.0f * grow};
        Color c = accent;
        c.a = static_cast<uint8_t>(accent.a / (i + 2));
        draw_round_rect(rr, radius + grow, c, /*filled*/ false);
    }
}

} // namespace gfx
