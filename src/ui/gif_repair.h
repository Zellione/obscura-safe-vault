#pragma once

// Lazy self-healing for ImageMeta::animated (Phase 47).
//
// GIFs stored before Phase 47 have animated = false regardless of content. When
// the viewer opens a GIF it has already decrypted for display, it re-runs the
// detector on those bytes and persists a correction if the stored flag is
// wrong. Mirrors ui/video_repair.* — no bulk rescan, no migration step, no
// user-visible action.

#include <cstdint>
#include <span>
#include <string_view>

namespace vault {
class Vault;
struct IndexNode;
}  // namespace vault

namespace ui {

// Returns true iff a correction was persisted.
bool maybe_repair_gif_animated(vault::Vault& v, std::string_view gallery_path,
                               const vault::IndexNode& node,
                               std::span<const uint8_t> data);

}  // namespace ui
