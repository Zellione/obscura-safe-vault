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
#include <unordered_set>

namespace vault {
class Vault;
struct IndexNode;
}  // namespace vault

namespace ui {

// Returns true iff a correction was persisted.
bool maybe_repair_gif_animated(vault::Vault& v, std::string_view gallery_path,
                               const vault::IndexNode& node,
                               std::span<const uint8_t> data);

// Gates the viewer's legacy-flag sniff, which costs a full read + decrypt of
// the image. Only a GIF whose animated flag is unset can need the pre-Phase-47
// repair (import and repair both persist the value sniffed from the actual
// bytes, so a set flag is trustworthy), and each such chunk needs it at most
// once per session — without this, every navigation onto a GIF re-read the
// whole image just to re-confirm a flag that cannot have changed.
class GifSniffGate {
public:
    [[nodiscard]] bool should_sniff(const vault::IndexNode& node);

private:
    std::unordered_set<uint64_t> sniffed_;  // keyed by data_offset
};

}  // namespace ui
