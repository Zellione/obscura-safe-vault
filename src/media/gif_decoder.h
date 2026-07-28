#pragma once

#ifdef OSV_VENDORED_AV

#include <memory>

// Streaming animated-GIF frame decoder (Phase 47); one media::AnimDecoder
// backend since Phase 57.
//
// MemAvio over a plaintext in-memory buffer -> FFmpeg's gif demuxer -> gif
// decoder -> swscale to RGBA, which is what the existing image texture pipeline
// wants (no YUV texture path is involved). FFmpeg's gif decoder performs frame
// disposal and composition internally, so every emitted frame is complete.
//
// Gated on OSV_VENDORED_AV — a build without vendored FFmpeg has no GIF
// animation (WebP animation still works; see media/webp_anim_decoder.h). The
// interface contract, including open()'s borrow semantics, lives in
// media/anim_decoder.h.

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "media/anim_decoder.h"

namespace media {

class GifDecoder final : public AnimDecoder {
public:
    GifDecoder();
    ~GifDecoder() override;

    // Opens `data` as a GIF. Returns false if it is not a decodable GIF.
    [[nodiscard]] bool open(std::span<const uint8_t> data) override;

    // Next frame, or nullopt at end of stream / on a decode error.
    [[nodiscard]] std::optional<AnimFrame> next_frame() override;

    // Seek back to the first frame, so the caller can loop.
    void rewind() override;

    [[nodiscard]] int    width()  const noexcept override;
    [[nodiscard]] int    height() const noexcept override;
    [[nodiscard]] size_t frames_decoded() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace media

#endif // OSV_VENDORED_AV
