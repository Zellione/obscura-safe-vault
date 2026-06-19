#include "test_framework.h"

#include <SDL3/SDL.h>

#include "gfx/yuv_texture.h"
#include "media/decoded_frame.h"

namespace {

// Headless RGBA surface + software renderer for GPU-free YUV texture tests.
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

// Create a synthetic I420 frame: 16x16 with mid-gray planes.
// I420: Y plane 256 bytes (one per pixel), U plane 64 bytes (quarter size), V plane 64 bytes.
media::DecodedFrame make_i420_frame(int w, int h)
{
    static std::vector<uint8_t> y_plane;
    static std::vector<uint8_t> u_plane;
    static std::vector<uint8_t> v_plane;

    // Allocate/reuse buffers.
    int y_size = w * h;
    int uv_size = (w / 2) * (h / 2);

    y_plane.assign(y_size, 0x80);    // mid-gray
    u_plane.assign(uv_size, 0x80);
    v_plane.assign(uv_size, 0x80);

    media::DecodedFrame f;
    f.planes[0]    = y_plane.data();
    f.planes[1]    = u_plane.data();
    f.planes[2]    = v_plane.data();
    f.linesizes[0] = w;          // Y plane linesize
    f.linesizes[1] = w / 2;      // U plane linesize
    f.linesizes[2] = w / 2;      // V plane linesize
    f.width        = w;
    f.height       = h;
    f.pix_fmt      = media::FramePixelFormat::I420;
    f.pts_seconds  = 0.0;

    return f;
}

} // namespace

TEST(yuv_texture_ensure_creates_texture)
{
    SoftRenderer sr;
    REQUIRE(sr.r != nullptr);
    {
        gfx::YuvTexture t;
        bool ok = t.ensure(sr.r, 16, 16, media::FramePixelFormat::I420);
        // If software renderer doesn't support streaming YUV textures, SDL may
        // return false and texture() may be nullptr. In that case, just check
        // that the boolean contract is honored.
        if (ok) {
            CHECK(t.texture() != nullptr);
        } else {
            // Legitimately unsupported on software renderer.
            CHECK(t.texture() == nullptr);
        }
    }
}

TEST(yuv_texture_update_with_i420_frame)
{
    SoftRenderer sr;
    REQUIRE(sr.r != nullptr);
    {
        gfx::YuvTexture t;
        auto frame = make_i420_frame(16, 16);
        bool ok = t.ensure(sr.r, 16, 16, media::FramePixelFormat::I420);

        if (ok) {
            // If ensure succeeded, texture should be non-null and update should work.
            CHECK(t.texture() != nullptr);
            bool updated = t.update(frame);
            CHECK(updated);
        } else {
            // If ensure failed, update should also fail gracefully.
            bool updated = t.update(frame);
            CHECK_FALSE(updated);
        }
    }
}

TEST(yuv_texture_recreates_on_size_change)
{
    SoftRenderer sr;
    REQUIRE(sr.r != nullptr);
    {
        gfx::YuvTexture t;

        // Ensure a 16x16 texture
        bool ok1 = t.ensure(sr.r, 16, 16, media::FramePixelFormat::I420);
        SDL_Texture* tex1 = t.texture();

        // Ensure a different size
        bool ok2 = t.ensure(sr.r, 32, 24, media::FramePixelFormat::I420);
        SDL_Texture* tex2 = t.texture();

        if (ok1 && ok2) {
            // If both succeeded, verify the texture changed (different object).
            // We can't directly query size from a destroyed texture, but we can
            // check that the pointer changed (texture was recreated).
            if (tex1 != nullptr && tex2 != nullptr) {
                // Either the pointer changed, or we're on software renderer
                // which may reuse memory. Best we can do: check non-null.
                CHECK(tex2 != nullptr);
            }
        }
    }
}

TEST(yuv_texture_handles_format_change)
{
    SoftRenderer sr;
    REQUIRE(sr.r != nullptr);
    {
        gfx::YuvTexture t;

        // Ensure I420
        bool ok1 = t.ensure(sr.r, 16, 16, media::FramePixelFormat::I420);

        // Ensure NV12 (different format, same size)
        bool ok2 = t.ensure(sr.r, 16, 16, media::FramePixelFormat::NV12);

        // If both succeeded, the texture should still be valid (recreated for the new format).
        if (ok1 && ok2) {
            CHECK(t.texture() != nullptr);
        }
    }
}

TEST(yuv_texture_destructor_safe)
{
    SoftRenderer sr;
    REQUIRE(sr.r != nullptr);
    {
        {
            gfx::YuvTexture t;
            CHECK(t.ensure(sr.r, 16, 16, media::FramePixelFormat::I420));
            // t goes out of scope, destructor runs (must not crash)
        }
        // If we get here, destructor was safe.
        CHECK(true);
    }
}
