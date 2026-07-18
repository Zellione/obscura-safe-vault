#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "vault/op_progress.h"   // OpProgress (background progress + cancel)
#include "vault/vault.h"         // Vault, VaultResult

namespace vault {

// Move (default) or Copy. Copy leaves the source untouched; Move removes it after
// the destination commit. Same code path for cross-vault and same-vault transfers.
enum class TransferMode { Move, Copy };

// Result of a bulk media transfer: how many files committed to the destination
// and how many failed (skipped). done + failed == the number attempted, which is
// <= the input size when a cancel stopped the loop early.
struct TransferTally {
    int done   = 0;
    int failed = 0;
};

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
// `dst` under `dst_parent`, keeping the gallery's own name. Copy-then-(maybe-)delete:
// the subtree is recreated + every descendant media leaf (image/video) copied into
// `dst` FIRST, then for TransferMode::Move the source subtree is removed — so a crash
// mid-Move leaves a recoverable duplicate, never a loss. `&src == &dst` is allowed; a
// same-vault move into the source itself or any descendant is rejected (cycle). Media
// plaintext lives only in mlock'd memory (invariant #1).
//   NotFound      - src gallery missing / not a gallery
//   AlreadyExists - dst_parent already holds a child of the same name
//   InvalidArg    - dst_parent cannot hold a sub-gallery (it holds media),
//                   src_gallery is the root (""), or a same-vault cycle
//   AuthFailed / IoError / Locked - propagated; source left intact if any copy fails.
// `progress` (optional): total is set to the subtree's media count up front, done
// bumped per copied file, and the copy stops early when progress->cancel is set —
// in which case the SOURCE IS LEFT INTACT (no removal) even for Move, so a cancel
// leaves the already-copied files as a recoverable duplicate, never a loss.
[[nodiscard]] VaultResult transfer_gallery(Vault& src, std::string_view src_gallery,
                                           Vault& dst, std::string_view dst_parent,
                                           TransferMode mode, OpProgress* progress = nullptr);

// Transfer a LIST of whole gallery subtrees (`src_paths`, all direct entries
// anywhere in `src`) into dst/dst_parent, one at a time via transfer_gallery
// (each an atomic copy-then-remove unit) — the bulk driver behind mass-moving
// multiple selected galleries at once (Phase 44 Part 3), mirroring
// transfer_images' loop over transfer_image. A failed subtree (NotFound/
// AlreadyExists/InvalidArg/etc.) is tallied as failed and left in place;
// others still proceed. `progress` (optional): total is set to
// src_paths.size() up front, done bumped per subtree, and the loop stops
// early on progress->cancel — subtrees moved so far remain committed.
[[nodiscard]] TransferTally transfer_galleries(Vault& src, const std::vector<std::string>& src_paths,
                                               Vault& dst, std::string_view dst_parent,
                                               TransferMode mode, OpProgress* progress = nullptr);

// Slash-paths of every gallery in `v` that may legally accept a SUB-gallery (i.e.
// holds no images), including "" (root) when root holds no images. Empty while
// locked. Used to populate the transfer dialog when the source is a gallery.
[[nodiscard]] std::vector<std::string> gallery_target_parents(const Vault& v);

} // namespace vault
