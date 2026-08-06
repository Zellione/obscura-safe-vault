#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "vault/op_progress.h"   // OpProgress (background progress + cancel)
#include "vault/vault.h"         // Vault, VaultResult

namespace vault {

// Move (default) or Copy. Copy leaves the source untouched; Move removes it after
// the destination commit. Same code path for cross-vault and same-vault transfers.
enum class TransferMode { Move, Copy };

inline constexpr size_t MAX_TRANSFER_FAILURES = 100;

// One item a transfer could not move/copy: the item's full slash-path IN THE
// SOURCE vault, the result code, and which side failed (Read = source
// decrypt/fetch, Write = destination add or source remove).
struct TransferFailure {
    enum class Stage : uint8_t { Read, Write };
    std::string path;
    VaultResult code  = VaultResult::Ok;
    Stage       stage = Stage::Write;
};

// Result of a bulk media transfer: how many files committed to the destination
// and how many failed (skipped). done + failed == the number attempted, which is
// <= the input size when a cancel stopped the loop early.
struct TransferTally {
    int done   = 0;
    int failed = 0;
    std::vector<TransferFailure> failures;   // first MAX_TRANSFER_FAILURES only
};

// Bump `failed` and store the entry while under MAX_TRANSFER_FAILURES (further
// failures are counted but not stored).
void record_failure(TransferTally& t, std::string path, VaultResult code,
                    TransferFailure::Stage stage);

// Transfer one media (image or video) from `src` (gallery `src_gallery`, file `filename`)
// into `dst`'s `dst_gallery`, keeping the filename. Reads the source plaintext into an
// mlock'd SecureBytes, re-encrypts it into `dst` via add_image or add_video (which
// regenerates the thumbnail/poster + metadata); for TransferMode::Move it then removes
// the source. `dst` is committed before `src` is mutated, so a crash mid-Move leaves
// the media in BOTH vaults (a recoverable duplicate) rather than losing it. `&src == &dst`
// is allowed (same-vault transfer). Plaintext lives only in the locked buffer (invariant #1).
//   NotFound      - src media, src gallery, or dst gallery missing
//   AlreadyExists - dst_gallery already holds `filename`
//   InvalidArg    - dst_gallery is not a leaf that can accept media
//   AuthFailed / IoError / Locked / CryptoError - propagated; source left intact if
//                   the destination add fails.
[[nodiscard]] VaultResult transfer_image(Vault& src, std::string_view src_gallery,
                                         std::string_view filename,
                                         Vault& dst, std::string_view dst_gallery,
                                         TransferMode mode);

// Transfer a list of media (`filenames`, all in src/src_gallery) into dst/dst_gallery,
// one file at a time via transfer_image (each an atomic copy-then-remove unit). This is
// the bulk driver behind the move/copy dialog, kept here (not in the UI) so it is
// headlessly testable. With `progress != nullptr`: total is set to filenames.size()
// up front, done is bumped after each file, and the loop stops early when
// progress->cancel is set — files transferred so far remain committed (a clean
// partial). Returns {committed, failed}; failed files are left in the source.
[[nodiscard]] TransferTally transfer_images(Vault& src, std::string_view src_gallery,
                                            const std::vector<std::string>& filenames,
                                            Vault& dst, std::string_view dst_gallery,
                                            TransferMode mode, OpProgress* progress = nullptr);

// Slash-paths of every gallery in `v` that may legally accept media (images or videos)
// — holds no sub-galleries, including "" (root) when root holds no sub-galleries. Used
// to populate the transfer dialog's destination-gallery list. Empty while locked.
[[nodiscard]] std::vector<std::string> image_target_galleries(const Vault& v);

// Transfer a whole gallery subtree from `src` (the gallery at `src_gallery`) into
// `dst` under `dst_parent`, keeping the gallery's own name. Per-file tolerance: copy
// every file; failures are recorded in `tally` (when given) rather than aborting.
// Copy-then-remove: for TransferMode::Move each file is removed from src immediately
// after its destination add commits (per-file Move). After all files, if no cancel and
// all copied OK, empty galleries are pruned bottom-up from the source (residue tree).
// `&src == &dst` is allowed; a same-vault move into the source itself or any
// descendant is rejected (cycle). Media plaintext lives only in mlock'd memory (invariant #1).
//   Ok            - transfer completed (per-file failures recorded in tally, if given).
//   NotFound      - src gallery missing / not a gallery
//   AlreadyExists - dst_parent already holds a child of the same name
//   InvalidArg    - dst_parent cannot hold a sub-gallery, src_gallery is root (""),
//                   same-vault cycle, or a sub-gallery's create_gallery failed structurally
//                   (source left intact; per-file Move never ran).
//   AuthFailed / IoError / Locked - propagated from structural failures; source left
//                   intact if the subtree root's create_gallery fails.
// `progress` (optional): total is set to the subtree's media count up front, done
// bumped per copied file, and the copy stops early when progress->cancel is set.
// A cancel stops cleanly: items moved so far live only in dst (their source copies
// removed per-file), the rest only in src — no duplicates, nothing lost. Pruning is
// skipped on cancel (galleries not yet recreated at dst must survive). Plaintext lives
// only in mlock'd memory (invariant #1).
// `tally` (optional): per-file failures are recorded; done/failed tally counts media
// (not galleries). Failed files trigger a failure entry; a failed gallery create
// increments failed by its media count but stores one gallery entry (not per-file).
[[nodiscard]] VaultResult transfer_gallery(Vault& src, std::string_view src_gallery,
                                           Vault& dst, std::string_view dst_parent,
                                           TransferMode mode, OpProgress* progress = nullptr,
                                           TransferTally* tally = nullptr);

// Transfer a LIST of whole gallery subtrees (`src_paths`, all direct entries
// anywhere in `src`) into dst/dst_parent, one at a time via transfer_gallery
// (each an atomic copy-then-remove unit) — the bulk driver behind mass-moving
// multiple selected galleries at once (Phase 44 Part 3), mirroring
// transfer_images' loop over transfer_image. Each subtree's per-file failures
// (from its tally) are merged into the returned tally. Structural failures
// (NotFound/AlreadyExists/InvalidArg) are recorded as gallery-level entries
// and left in place; others still proceed. `progress` (optional): total is set
// to src_paths.size() up front, done bumped per subtree, and the loop stops
// early on progress->cancel — subtrees moved so far remain committed.
[[nodiscard]] TransferTally transfer_galleries(Vault& src, const std::vector<std::string>& src_paths,
                                               Vault& dst, std::string_view dst_parent,
                                               TransferMode mode, OpProgress* progress = nullptr);

// Slash-paths of every gallery in `v` that may legally accept a SUB-gallery (i.e.
// holds no images), including "" (root) when root holds no images. Empty while
// locked. Used to populate the transfer dialog when the source is a gallery.
[[nodiscard]] std::vector<std::string> gallery_target_parents(const Vault& v);

} // namespace vault
