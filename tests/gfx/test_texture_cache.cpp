#include "test_framework.h"

#include <SDL3/SDL.h>

#include "gfx/texture_cache.h"
#include "image/image.h"

namespace {

// Headless RGBA surface + software renderer for GPU-free texture tests.
struct SoftRenderer {
    SDL_Surface*  surf = nullptr;
    SDL_Renderer* r    = nullptr;
    SoftRenderer()
    {
        surf = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA32);
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

// Each cached entry is accounted as w*h*4 bytes (RGBA on the GPU).
constexpr std::size_t kEntryBytes = 64u * 64u * 4u; // 16384

} // namespace

TEST(texture_cache_uploads_one_pixel)
{
    SoftRenderer sr;
    REQUIRE(sr.r != nullptr);
    {
        gfx::TextureCache cache(sr.r);
        SDL_Texture* t = cache.get_or_upload(1, solid(1, 1, 200));
        CHECK(t != nullptr);
        CHECK(cache.contains(1));
        CHECK_EQ(cache.count(), std::size_t{1});
        CHECK(cache.bytes() > 0);
    }
}

TEST(texture_cache_returns_same_texture_for_key)
{
    SoftRenderer sr;
    REQUIRE(sr.r != nullptr);
    {
        gfx::TextureCache cache(sr.r);
        SDL_Texture* a = cache.get_or_upload(7, solid(8, 8, 100));
        SDL_Texture* b = cache.get_or_upload(7, solid(8, 8, 100));
        CHECK(a == b);
        CHECK_EQ(cache.count(), std::size_t{1});
        CHECK(cache.get(7) == a);
        CHECK(cache.get(999) == nullptr);
    }
}

TEST(texture_cache_evicts_least_recently_used)
{
    SoftRenderer sr;
    REQUIRE(sr.r != nullptr);
    {
        // Budget holds exactly two 64x64 entries.
        gfx::TextureCache cache(sr.r, 2 * kEntryBytes);
        cache.get_or_upload(1, solid(64, 64, 10));
        cache.get_or_upload(2, solid(64, 64, 20));
        CHECK_EQ(cache.count(), std::size_t{2});
        CHECK_EQ(cache.bytes(), 2 * kEntryBytes);

        // Third upload exceeds the budget → the LRU (key 1) is evicted.
        cache.get_or_upload(3, solid(64, 64, 30));
        CHECK_EQ(cache.count(), std::size_t{2});
        CHECK_FALSE(cache.contains(1));
        CHECK(cache.contains(2));
        CHECK(cache.contains(3));
    }
}

TEST(texture_cache_get_marks_most_recently_used)
{
    SoftRenderer sr;
    REQUIRE(sr.r != nullptr);
    {
        gfx::TextureCache cache(sr.r, 2 * kEntryBytes);
        cache.get_or_upload(1, solid(64, 64, 10));
        cache.get_or_upload(2, solid(64, 64, 20));

        // Touch key 1 so key 2 becomes the LRU.
        CHECK(cache.get(1) != nullptr);

        cache.get_or_upload(3, solid(64, 64, 30));
        CHECK(cache.contains(1));
        CHECK_FALSE(cache.contains(2));
        CHECK(cache.contains(3));
    }
}

TEST(texture_cache_clear_empties)
{
    SoftRenderer sr;
    REQUIRE(sr.r != nullptr);
    {
        gfx::TextureCache cache(sr.r);
        cache.get_or_upload(1, solid(16, 16, 5));
        cache.get_or_upload(2, solid(16, 16, 5));
        cache.clear();
        CHECK_EQ(cache.count(), std::size_t{0});
        CHECK_EQ(cache.bytes(), std::size_t{0});
        CHECK_FALSE(cache.contains(1));
    }
}

TEST(texture_cache_rejects_empty_image)
{
    SoftRenderer sr;
    REQUIRE(sr.r != nullptr);
    {
        gfx::TextureCache cache(sr.r);
        image::ImageData empty; // width/height 0, no pixels
        CHECK(cache.get_or_upload(1, empty) == nullptr);
        CHECK_EQ(cache.count(), std::size_t{0});
    }
}
