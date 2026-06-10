#include "test_framework.h"

#include <SDL3/SDL.h>

#include <array>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/texture_cache.h"
#include "image/image.h"

namespace {

constexpr const char* kFontPath = OSV_DEFAULT_FONT;

struct SoftRenderer {
    SDL_Surface*  surf = nullptr;
    SDL_Renderer* r    = nullptr;
    SoftRenderer()
    {
        surf = SDL_CreateSurface(256, 128, SDL_PIXELFORMAT_RGBA32);
        if (surf) r = SDL_CreateSoftwareRenderer(surf);
    }
    ~SoftRenderer()
    {
        if (r)    SDL_DestroyRenderer(r);
        if (surf) SDL_DestroySurface(surf);
    }
};

image::ImageData solid(int w, int h, uint8_t v)
{
    image::ImageData d;
    d.width  = w;
    d.height = h;
    d.format = image::ImageFormat::PNG;
    d.pixels.assign(static_cast<size_t>(w) * h * 3, v);
    return d;
}

} // namespace

// ---------------------------------------------------------------------------
// Pure layout maths
// ---------------------------------------------------------------------------

TEST(thumbnail_strip_content_width_math)
{
    CHECK_EQ(gfx::thumbnail_strip_content_width(0, 100.0f, 8.0f), 0.0f);
    CHECK_EQ(gfx::thumbnail_strip_content_width(1, 100.0f, 8.0f), 100.0f);
    // 3 cells of 100 with 2 gaps of 8 = 316.
    CHECK_EQ(gfx::thumbnail_strip_content_width(3, 100.0f, 8.0f), 316.0f);
}

// ---------------------------------------------------------------------------
// Draw smoke tests against a headless software renderer
// ---------------------------------------------------------------------------

TEST(renderer_draw_rect_and_image_run_headless)
{
    SoftRenderer sr;
    REQUIRE(sr.r != nullptr);
    {
        gfx::Renderer rr(sr.r);
        gfx::TextureCache cache(sr.r);

        SDL_SetRenderDrawColor(sr.r, 0, 0, 0, 255);
        SDL_RenderClear(sr.r);

        rr.draw_rect(SDL_FRect{2, 2, 40, 20}, gfx::Color{200, 30, 30, 255});
        rr.draw_rect(SDL_FRect{50, 2, 40, 20}, gfx::Color{30, 200, 30, 255}, false);

        SDL_Texture* tex = cache.get_or_upload(1, solid(16, 16, 180));
        CHECK(tex != nullptr);
        rr.draw_image(tex, SDL_FRect{100, 10, 32, 32});

        SDL_RenderPresent(sr.r);
    }
}

TEST(renderer_draw_thumbnail_strip_runs_headless)
{
    SoftRenderer sr;
    REQUIRE(sr.r != nullptr);
    {
        gfx::Renderer rr(sr.r);
        gfx::TextureCache cache(sr.r);

        std::array<SDL_Texture*, 4> thumbs{
            cache.get_or_upload(1, solid(20, 20, 60)),
            cache.get_or_upload(2, solid(20, 20, 90)),
            cache.get_or_upload(3, solid(20, 20, 120)),
            cache.get_or_upload(4, solid(20, 20, 150)),
        };

        const SDL_FRect strip{0, 96, 256, 32};
        const float content = rr.draw_thumbnail_strip(
            thumbs, strip, /*thumb_size=*/28.0f, /*gap=*/4.0f,
            /*scroll_x=*/10.0f, /*selected=*/2, gfx::Color{255, 255, 0, 255});

        // 4 cells of 28 + 3 gaps of 4 = 124.
        CHECK_EQ(content, 124.0f);
    }
}

TEST(renderer_draw_text_runs_headless)
{
    SoftRenderer sr;
    REQUIRE(sr.r != nullptr);
    {
        gfx::Renderer rr(sr.r);
        gfx::FontAtlas font;
        REQUIRE(font.bake_from_file(kFontPath, 18.0f));

        SDL_SetRenderDrawColor(sr.r, 0, 0, 0, 255);
        SDL_RenderClear(sr.r);
        rr.draw_text(font, 4.0f, 4.0f, "Vault 123", gfx::Color{255, 255, 255, 255});
        SDL_RenderPresent(sr.r);
    }
}
