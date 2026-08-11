#pragma once

// Phase 65: what upgrade work a vault still owes, and the watermark that
// records having done it.
//
// Detection is a PURE TREE WALK with zero I/O — no decrypt, no file reads — so
// the unlock-time offer is instant regardless of vault size. Nothing here
// depends on src/media/ or SDL: the caps generation is passed in by the caller
// (see media::PROBE_CAPS_GEN), which keeps vault/ independent of the decoder
// layer and makes the staleness rule testable without FFmpeg.

#include <cstddef>
#include <cstdint>

#include "vault/index.h"

namespace vault {

class Vault;

// Counts of nodes that still owe backfill work, and the plaintext bytes the
// migration would have to read to resolve them.
struct MigrationScan {
    size_t   videos = 0;   // is_video() && vmeta.codec == VideoCodec::Unknown
    size_t   images = 0;   // is_image() && format_can_animate() && !meta.animated
    size_t   thumbs = 0;   // Phase 75: thumbs/posters to regenerate at the new budget
    uint64_t bytes  = 0;   // total orig_size over both arms

    [[nodiscard]] bool   empty() const noexcept { return videos == 0 && images == 0 && thumbs == 0; }
    [[nodiscard]] size_t total() const noexcept { return videos + images + thumbs; }
};

// True when this build knows a backfill this vault has not recorded running.
[[nodiscard]] bool migration_pending(const VaultSettings& s, uint16_t probe_caps_gen,
                                     uint16_t thumb_side) noexcept;

// Stamp `s` as fully migrated by this build. Returned by value; the caller
// persists it via vault::set_vault_settings.
[[nodiscard]] VaultSettings stamp_migrated(VaultSettings s, uint16_t probe_caps_gen,
                                           uint16_t thumb_side) noexcept;

// Walk the whole tree and count outstanding work. Main-thread only (touches the
// index tree via Vault::list). No I/O.
[[nodiscard]] MigrationScan scan_migration(const Vault& v, bool thumbs_stale);

} // namespace vault
