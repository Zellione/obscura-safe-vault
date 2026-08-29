#pragma once

#include <cstdint>
#include <optional>

#include "crypto/secure_mem.h"
#include "image/image.h"

namespace image {

// Stored-thumbnail budget: max long-side (px) for gallery thumbnails and video
// posters kept inside the vault. Raised 256 -> 512 in Phase 75 so GridL/GridXL
// tiles (352/448 px) render sharp; existing vaults regenerate via the Phase 65
// migration framework (VaultSettings::migrated_thumb_side watermark).
inline constexpr int THUMB_MAX_SIDE = 512;

// Downscale `src` so neither dimension exceeds `max_side` (no upscaling if already
// smaller), encode the result as JPEG at `quality` (0-100), and return the bytes.
// Returns nullopt if `src` is empty or resize/encode fails.
//
// Phase 96 (OSV-AUD-003): the returned JPEG — and the resized RGB pixels it is
// derived from — live in mlock'd, wipe-on-release SecureBytes, never in ordinary
// std::vector<uint8_t>. The stbi_write callback appends into the growable secure
// buffer, and an allocation failure mid-encode yields nullopt (never a truncated
// JPEG).
[[nodiscard]] std::optional<crypto::SecureBytes>
make_thumbnail(const ImageData& src, int max_side, int quality = 85);

} // namespace image