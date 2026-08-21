#include "gfx/yuv_texture.h"

#include "platform/safe_print.h"

namespace gfx {

YuvTexture::~YuvTexture()
{
    if (tex_) {
        SDL_DestroyTexture(tex_);
        tex_ = nullptr;
    }
}

bool YuvTexture::ensure(SDL_Renderer* r, int w, int h,
                        media::FramePixelFormat fmt)
{
    // If texture already exists with the same dimensions and format, keep it.
    if (tex_ && width_ == w && height_ == h && pix_fmt_ == fmt) {
        return true;
    }

    // Destroy old texture if it exists.
    if (tex_) {
        SDL_DestroyTexture(tex_);
        tex_ = nullptr;
    }

    // Determine SDL pixel format from the enum.
    SDL_PixelFormat sdl_fmt = (fmt == media::FramePixelFormat::I420)
                                  ? SDL_PIXELFORMAT_IYUV
                                  : SDL_PIXELFORMAT_NV12;

    // Create streaming texture.
    tex_ = SDL_CreateTexture(r, sdl_fmt, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!tex_) {
        platform::safe_println(stderr, "[YuvTexture] SDL_CreateTexture failed: {}",
                     SDL_GetError());
        return false;
    }

    // Cache the dimensions and format.
    width_   = w;
    height_  = h;
    pix_fmt_ = fmt;

    return true;
}

bool YuvTexture::update(const media::DecodedFrame& f)
{
    // Require a texture to exist.
    if (!tex_) {
        return false;
    }

    // Require size and format match.
    if (f.width != width_ || f.height != height_ || f.pix_fmt != pix_fmt_) {
        return false;
    }

    // Update based on pixel format.
    bool ok = false;
    if (pix_fmt_ == media::FramePixelFormat::I420) {
        // I420: Y, U, V planes with separate linesizes.
        ok = SDL_UpdateYUVTexture(tex_, nullptr,
                                  f.planes[0], f.linesizes[0],  // Y plane
                                  f.planes[1], f.linesizes[1],  // U plane
                                  f.planes[2], f.linesizes[2]); // V plane
    } else {
        // NV12: Y plane, then interleaved UV plane.
        ok = SDL_UpdateNVTexture(tex_, nullptr,
                                 f.planes[0], f.linesizes[0],  // Y plane
                                 f.planes[1], f.linesizes[1]); // UV plane
    }

    return ok;
}

} // namespace gfx
