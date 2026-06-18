#pragma once

#include <SDL3/SDL.h>

#include "media/decoded_frame.h"

namespace gfx {

/// YuvTexture: a streaming YUV texture helper for SDL3.
/// Owns a single SDL_Texture* (SDL_TEXTUREACCESS_STREAMING) and manages
/// recreation when width, height, or pixel format changes.
/// Does not own the SDL_Renderer; the renderer must outlive this object.
class YuvTexture {
public:
    YuvTexture() = default;
    ~YuvTexture();

    YuvTexture(const YuvTexture&)            = delete;
    YuvTexture& operator=(const YuvTexture&) = delete;
    YuvTexture(YuvTexture&&)                 = delete;
    YuvTexture& operator=(YuvTexture&&)      = delete;

    /// Ensure a texture of the given dimensions and format exists.
    /// If no texture exists yet, or if w, h, or fmt differ from the current texture,
    /// destroy the old texture and create a new one. Returns false on creation failure
    /// (logged to stderr with [YuvTexture] prefix).
    [[nodiscard]] bool ensure(SDL_Renderer* r, int w, int h,
                              media::FramePixelFormat fmt);

    /// Update the texture with the pixel data from a decoded frame.
    /// Requires a matching texture to exist (same size and format).
    /// Uses SDL_UpdateYUVTexture for I420 or SDL_UpdateNVTexture for NV12.
    /// Returns false if no texture exists, size/format mismatch, or update fails.
    [[nodiscard]] bool update(const media::DecodedFrame& f);

    /// Return the underlying SDL_Texture*, or nullptr if none exists.
    [[nodiscard]] SDL_Texture* texture() const noexcept { return tex_; }

private:
    SDL_Texture* tex_ = nullptr;
    int          width_  = 0;
    int          height_ = 0;
    media::FramePixelFormat pix_fmt_ = media::FramePixelFormat::I420;
};

} // namespace gfx
