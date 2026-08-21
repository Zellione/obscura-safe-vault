#pragma once

// Animated-image detection — GIF (Phase 47) and WebP (Phase 57).
//
// The vault stores the verdict as ImageMeta::animated and the UI draws the "A"
// badge from it, both of which must work in builds without vendored FFmpeg, so
// nothing here is gated on OSV_VENDORED_AV.
//
// A vault file is untrusted input (AGENTS.md security invariant 6). The GIF walk
// is fully bounds-checked: truncated, malformed, or non-GIF input returns false
// rather than reading past the buffer. The WebP probe deliberately delegates to
// libwebp's own header parser instead of hand-rolling a RIFF walker — libwebp
// parses these bytes anyway, and it is fuzzed upstream.

#include <cstdint>
#include <span>

#include "image/image.h"   // image::ImageFormat

namespace image {

[[nodiscard]] bool gif_is_animated(std::span<const uint8_t> data) noexcept;
[[nodiscard]] bool webp_is_animated(std::span<const uint8_t> data) noexcept;

// Dispatches to the detector for `fmt`. A format that cannot carry an animation
// returns false without inspecting the bytes.
[[nodiscard]] bool is_animated(ImageFormat fmt, std::span<const uint8_t> data) noexcept;

} // namespace image
