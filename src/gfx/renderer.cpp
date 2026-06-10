#include "gfx/renderer.h"

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
                                     const SDL_FRect& strip, float thumb_size,
                                     float gap, float scroll_x, int selected,
                                     Color highlight)
{
    SDL_Rect clip{static_cast<int>(strip.x), static_cast<int>(strip.y),
                  static_cast<int>(strip.w), static_cast<int>(strip.h)};
    SDL_SetRenderClipRect(r_, &clip);

    const float top = strip.y + (strip.h - thumb_size) * 0.5f;
    for (int i = 0; i < static_cast<int>(thumbs.size()); ++i) {
        const float cell_x = strip.x - scroll_x +
                             static_cast<float>(i) * (thumb_size + gap);
        // Skip cells entirely outside the strip.
        if (cell_x + thumb_size < strip.x || cell_x > strip.x + strip.w)
            continue;

        const SDL_FRect dst{cell_x, top, thumb_size, thumb_size};
        if (thumbs[i]) {
            SDL_SetTextureColorMod(thumbs[i], 255, 255, 255);
            SDL_SetTextureAlphaMod(thumbs[i], 255);
            SDL_RenderTexture(r_, thumbs[i], nullptr, &dst);
        }
        if (i == selected) {
            SDL_SetRenderDrawColor(r_, highlight.r, highlight.g, highlight.b,
                                   highlight.a);
            SDL_RenderRect(r_, &dst);
        }
    }

    SDL_SetRenderClipRect(r_, nullptr);
    return thumbnail_strip_content_width(static_cast<int>(thumbs.size()),
                                         thumb_size, gap);
}

} // namespace gfx
