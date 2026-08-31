#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "vault/chunk_ref.h"   // vault::ChunkRef
#include "vault/index.h"

// Pure, SDL-free, vault-free cover resolution for gallery tiles (Phase 19).
// Walks the in-memory index tree and returns *thumbnail chunk references* only
// — it reads nothing, decodes nothing, and touches no disk. Rendering decrypts
// the returned refs on demand via the existing thumbnail texture cache.
namespace ui {

// Location + Phase 99 identity of one cover's thumbnail chunk in the vault's
// data region. `length` of 0 is never returned (such a node is skipped during
// resolution).
struct CoverSpan {
    vault::ChunkRef ref;

    [[nodiscard]] bool operator==(const CoverSpan&) const noexcept = default;
};

// The single representative cover for `gallery`, resolved recursively:
//   * a gallery holding media -> the first media child with a stored thumbnail
//     (an image's thumbnail span, or a video's poster span);
//   * a gallery holding sub-galleries -> the first sub-gallery (in child order)
//     that itself resolves to a cover.
// Returns nullopt for an empty subtree (no usable thumbnail anywhere). Depth is
// bounded by `max_depth` (reuses the index depth limit) so a pathological tree
// can never overflow the stack.
[[nodiscard]] std::optional<CoverSpan> resolve_single_cover(
    const vault::IndexNode& gallery,
    uint32_t                max_depth = vault::INDEX_MAX_DEPTH);

// The cover list to render on a gallery TILE:
//   * a gallery holding media -> 0 or 1 cover (its first media thumbnail);
//   * a gallery holding sub-galleries -> up to 4 covers, one per sub-gallery in
//     child order, each its own single recursive cover (sub-galleries that
//     resolve to nothing are skipped).
// Empty when nothing resolves (caller falls back to the folder icon).
[[nodiscard]] std::vector<CoverSpan> resolve_covers(
    const vault::IndexNode& gallery,
    uint32_t                max_depth = vault::INDEX_MAX_DEPTH);

}  // namespace ui
