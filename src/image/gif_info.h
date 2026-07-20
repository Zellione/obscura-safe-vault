#pragma once

// Animated-GIF detection (Phase 47).
//
// Walks a GIF byte stream's block structure and reports whether it contains two
// or more frames. Deliberately dependency-free and NOT gated on OSV_VENDORED_AV:
// the vault stores the verdict as ImageMeta::animated and the UI draws the "A"
// badge from it, both of which must work in builds without vendored FFmpeg.
//
// A vault file is untrusted input (CLAUDE.md security invariant 6), so the walk
// is fully bounds-checked: truncated, malformed, or non-GIF input returns false
// rather than reading past the buffer.

#include <cstdint>
#include <span>

namespace image {

[[nodiscard]] bool gif_is_animated(std::span<const uint8_t> data) noexcept;

} // namespace image
