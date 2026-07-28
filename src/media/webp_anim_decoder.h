#pragma once

// Animated-WebP frame decoder (Phase 57) — a media::AnimDecoder backend built on
// libwebp's WebPAnimDecoder (libwebpdemux).
//
// Deliberately NOT gated on OSV_VENDORED_AV, unlike GifDecoder: libwebp is a
// hard dependency of the app, so animated WebP plays in every build.
//
// libwebp performs frame disposal and blending onto an internal canvas, so every
// emitted frame is a complete image. That canvas costs width*height*4 twice
// (current + previous) for the life of the decoder, and the whole compressed
// file must stay resident — it is the caller's mlock'd buffer, so no copy.
//
// Lifetime: open() BORROWS the caller's buffer (an mlock'd SecureBytes in
// production) and does not copy it. Keep it alive until the decoder is
// destroyed. See media/anim_decoder.h for the rest of the contract.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "media/anim_decoder.h"

namespace media {

// libwebp reports cumulative timestamps in milliseconds; playback needs a
// per-frame delay in seconds. Clamped to kMinFrameDelay, which also absorbs the
// 0 ms and non-monotonic timestamps malformed files declare. Pure, so the clamp
// is unit tested without hunting for a 0 ms fixture.
[[nodiscard]] double webp_frame_delay_s(int prev_timestamp_ms, int timestamp_ms) noexcept;

class WebpAnimDecoder final : public AnimDecoder {
public:
    WebpAnimDecoder();
    ~WebpAnimDecoder() override;

    // Returns false for a static WebP (a single frame is not an animation), a
    // non-WebP buffer, or a corrupt animation. Re-opening resets all state.
    [[nodiscard]] bool open(std::span<const uint8_t> data) override;

    [[nodiscard]] std::optional<AnimFrame> next_frame() override;

    void rewind() override;

    [[nodiscard]] int    width()  const noexcept override;
    [[nodiscard]] int    height() const noexcept override;
    [[nodiscard]] size_t frames_decoded() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace media
