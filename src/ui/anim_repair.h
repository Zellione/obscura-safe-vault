#pragma once

// Lazy self-healing for ImageMeta::animated (Phase 47 for GIF, Phase 57 for WebP).
//
// GIFs stored before Phase 47 have animated = false regardless of content. When
// the viewer opens an image it has already decrypted for display, it re-runs the
// detector on those bytes and persists a correction if the stored flag is wrong.
// Mirrors ui/video_repair.* — no bulk rescan, no migration step, no user-visible
// action.
//
// The WebP arm is unreachable for vaults written before Phase 57: an animated
// WebP could not be imported at all (decode failed), so no such node exists. It
// is kept format-generic anyway — it costs nothing, and it covers a vault whose
// writer classified a file differently.

#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_set>

namespace vault {
class Vault;
struct IndexNode;
}  // namespace vault

namespace ui {

// Returns true iff a correction was persisted.
bool maybe_repair_animated(vault::Vault& v, std::string_view gallery_path,
                           const vault::IndexNode& node,
                           std::span<const uint8_t> data);

// Gates the viewer's legacy-flag sniff, which costs a full read + decrypt of the
// image. Only an animatable format whose animated flag is unset can need the
// repair (import and repair both persist the value sniffed from the actual
// bytes, so a set flag is trustworthy), and each such chunk needs it at most
// once per session — without this, every navigation onto such an image re-read
// the whole thing just to re-confirm a flag that cannot have changed.
class AnimSniffGate {
public:
    [[nodiscard]] bool should_sniff(const vault::IndexNode& node);

private:
    std::unordered_set<uint64_t> sniffed_;  // keyed by data_offset
};

}  // namespace ui
