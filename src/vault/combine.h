#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "vault/op_progress.h"
#include "vault/vault.h"

namespace vault {

// Tally for a (possibly recursive) gallery combine: how much media moved vs.
// was skipped on a filename collision, and how many sub-galleries recursed
// (same name on both sides) vs. moved wholesale (no collision). Phase 44 Part 4.
struct CombineTally {
    int media_moved      = 0;
    int media_skipped     = 0;
    int galleries_merged  = 0;
    int galleries_moved   = 0;
};

// Merge `src_gallery`'s contents into `dst_gallery` (same or a different
// vault), deleting `src_gallery` once it ends up empty. Works purely through
// Vault's public API (list/add_image/add_video/read_image/read_video/
// add_tag/remove_gallery) — no new Vault friendship needed.
//
// Leaf case (src_gallery holds media): every media filename moves via
// transfer_image; a name already present in dst_gallery is skipped and
// tallied (media_skipped), not a hard failure.
//
// Folder case (src_gallery holds sub-galleries): for each sub-gallery child —
// no same-named child in dst_gallery -> moved wholesale via transfer_gallery
// (tallies galleries_moved); a same-named child exists -> recurse into this
// same function on that pair (tallies galleries_merged, plus whatever its
// own recursive tally reports).
//
// Tags: union — every one of src_gallery's own tags is added to dst_gallery
// via add_tag (already case-insensitively deduped).
//
// Source removal: src_gallery (and, recursively, any merged sub-gallery) is
// deleted ONLY if it ends up with zero children after processing — a
// partially-merged gallery (some media/sub-galleries skipped on collision) is
// left in place with its remaining children, so nothing already committed to
// dst is ever lost, and the caller can resolve conflicts (e.g. via
// vault::rename_node) and retry.
//
//   Locked      - either vault is not unlocked
//   InvalidArg  - src_gallery is the root (""); dst_gallery is src_gallery or
//                 a descendant of it (same-vault cycle); or the two galleries
//                 are structurally incompatible (one holds media, the other
//                 holds sub-galleries)
//   NotFound    - src_gallery or dst_gallery doesn't resolve to a gallery
//
// `progress` (optional): total is set ONCE, up front, to the full media count
// of src_gallery's subtree; done is bumped per media file moved directly, and
// by a whole subtree's media count in one step when that subtree moves
// wholesale (transfer_gallery is never handed this `progress` object itself —
// it would clobber `total` with its own subtree's count). A cancel mid-merge
// leaves everything moved so far in place and does not delete the source.
[[nodiscard]] VaultResult combine_galleries(Vault& src, std::string_view src_gallery,
                                            Vault& dst, std::string_view dst_gallery,
                                            CombineTally& tally, OpProgress* progress = nullptr);

// Every gallery in `dst` (any depth, including root) that `src_gallery` (in
// `src`) could legally combine into: structurally compatible (see
// combine_galleries' InvalidArg cases above), and — same-vault only — not
// `src_gallery` itself or a descendant of it. Used to populate the combine
// dialog's destination picker. Empty while either vault is locked.
[[nodiscard]] std::vector<std::string> combine_target_galleries(const Vault& dst, const Vault& src,
                                                                 std::string_view src_gallery);

} // namespace vault
