#include "ui/tile_thumb.h"

#include <algorithm>

#include "crypto/secure_mem.h"
#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/texture_cache.h"
#include "gfx/theme.h"
#include "image/decode_worker.h"
#include "ui/cover_layout.h"
#include "ui/gallery_cover.h"
#include "ui/widgets.h"          // fit_rect
#include "vault/index.h"
#include "vault/vault.h"

namespace ui {

ThumbKey thumb_key_for(const vault::IndexNode& node)
{
    if (node.is_video())
        return {node.vmeta.poster_offset, node.vmeta.poster_length > 0};
    return {node.meta.data_offset, node.meta.thumb_length > 0};
}

SDL_Texture* tile_thumb_texture(const ThumbContext& ctx, const vault::IndexNode& node)
{
    const auto [key, present] = thumb_key_for(node);
    if (!present) return nullptr;
    if (SDL_Texture* t = ctx.cache.get(key)) return t;

    // A thumbnail that already failed to decode is not retried; an in-flight
    // decode lands when the host screen pumps the worker. Otherwise read+decrypt
    // here (fast) and enqueue the slow decode off-thread.
    if (ctx.failed.contains(key) || ctx.worker.pending(key)) return nullptr;
    crypto::SecureBytes sb;
    if (ctx.vault.read_thumbnail(node, sb) != vault::VaultResult::Ok) return nullptr;
    ctx.worker.submit(key, std::move(sb));
    return nullptr;
}

SDL_Texture* tile_cover_tex(const ThumbContext& ctx, const CoverSpan& span)
{
    const uint64_t key = span.offset;
    if (SDL_Texture* t = ctx.cache.get(key)) return t;
    if (ctx.failed.contains(key) || ctx.worker.pending(key)) return nullptr;
    crypto::SecureBytes sb;
    if (vault::read_thumb_span(ctx.vault, span.offset, span.length, sb) != vault::VaultResult::Ok)
        return nullptr;
    ctx.worker.submit(key, std::move(sb));
    return nullptr;
}

void draw_tile_thumb(gfx::Renderer& r, gfx::FontAtlas& font, const ThumbContext& ctx,
                     const vault::IndexNode& n, const SDL_FRect& box)
{
    using namespace gfx::theme;
    if (n.is_gallery()) {
        // Draw a gold folder (body + tab) behind the cover so a gallery is
        // unmistakable from a plain image, then inset a representative cover
        // (leaf -> first image/poster) or a 2×2 montage of up to four sub-gallery
        // covers inside it. An empty subtree just shows the bare folder.
        const auto ff = folder_frame(box);
        r.draw_round_rect(ff.tab, RADIUS_SMALL, FOLDER);
        r.draw_round_rect(ff.body, RADIUS_SMALL, FOLDER);

        const auto covers = resolve_covers(n);
        if (covers.empty()) return;

        const auto cells =
            cover_montage_rects(ff.inner, static_cast<int>(covers.size()));
        for (size_t i = 0; i < cells.size(); ++i) {
            r.draw_rect(cells[i], gfx::Color{0, 0, 0, 255});   // backing, never stretched
            if (SDL_Texture* tex = tile_cover_tex(ctx, covers[i])) {
                float tw = 0;
                float th = 0;
                SDL_GetTextureSize(tex, &tw, &th);
                r.draw_image(tex, fit_rect(tw, th, cells[i]));
            }
        }
        return;
    }
    r.draw_rect(box, gfx::Color{0, 0, 0, 255});   // black backing, never stretched
    if (SDL_Texture* tex = tile_thumb_texture(ctx, n)) {
        float tw = 0;
        float th = 0;
        SDL_GetTextureSize(tex, &tw, &th);
        r.draw_image(tex, fit_rect(tw, th, box));
    } else {
        r.draw_text(font, box.x + 6, box.y + box.h * 0.5f - 14, "(no thumb)", TEXT_DIM);
    }
    // Video: a centred play-triangle badge over the poster (with a 1px dark
    // drop-shadow for contrast — the renderer's default blend mode is opaque).
    if (n.is_video()) {
        const float cx = box.x + box.w * 0.5f;
        const float cy = box.y + box.h * 0.5f;
        const float s  = std::min(box.w, box.h) * 0.16f;
        const SDL_FPoint a{cx - s * 0.5f, cy - s};
        const SDL_FPoint b{cx - s * 0.5f, cy + s};
        const SDL_FPoint c{cx + s, cy};
        r.draw_triangle({a.x + 2, a.y + 2}, {b.x + 2, b.y + 2}, {c.x + 2, c.y + 2},
                        gfx::Color{0, 0, 0, 255});
        r.draw_triangle(a, b, c, gfx::Color{255, 255, 255, 255});
    }
}


bool tile_shows_animated_badge(const vault::IndexNode& node) noexcept
{
    return node.type == vault::IndexNode::Type::Image
        && node.meta.format == vault::ImageFormat::GIF
        && node.meta.animated;
}

void draw_animated_badge(gfx::Renderer& r, gfx::FontAtlas& font,
                        const SDL_FRect& tile_rect, float badge_size,
                        float x_offset, float y_offset) noexcept
{
    // Position badge in top-right corner of the tile, shifted left/down by
    // x_offset/y_offset if needed (e.g. to avoid overlapping a favorite marker).
    const SDL_FRect badge{tile_rect.x + tile_rect.w - badge_size - 8 + x_offset,
                         tile_rect.y + y_offset, badge_size, badge_size};

    // Text offsets scale with badge size: roughly 1/4 of the badge width/height.
    const float text_x_offset = badge_size * 0.22f;
    const float text_y_offset = badge_size * 0.15f;

    r.draw_round_rect(badge, gfx::theme::RADIUS_SMALL, gfx::theme::ACCENT);
    r.draw_round_rect(badge, gfx::theme::RADIUS_SMALL, gfx::theme::BG, /*filled*/ false);
    r.draw_text(font, badge.x + text_x_offset, badge.y + text_y_offset, "A", gfx::theme::TEXT);
}

} // namespace ui
