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
    int done    = 0;
    int failed  = 0;
    int skipped = 0;   // destination-name collisions (file left in the source)
    std::vector<TransferFailure> failures;   // first MAX_TRANSFER_FAILURES only
};

// Bump `failed` and store the entry while under MAX_TRANSFER_FAILURES (further
// failures are counted but not stored).
void record_failure(TransferTally& t, std::string path, VaultResult code,
                    TransferFailure::Stage stage);

// Transfer one media (image or video) from `src` (gallery `src_gallery`, file `filename`)
// into `dst`'s `dst_gallery`, keeping the filename. Reads the source plaintext into an
// mlock'd SecureBytes and re-encrypts it into `dst` with the source's own thumbnail/
// poster + metadata (Phase 67: no re-decode, no re-probe); for TransferMode::Move it
// then removes the source. `dst` is committed before `src` is mutated, so a crash mid-Move leaves
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

// Transfer a list of media (`filenames`, all in src/src_gallery) into dst/dst_gallery.
// This is the bulk driver behind the move/copy dialog, kept here (not in the UI) so it
// is headlessly testable. Batched (Phase 69): destination durability is ONE commit per
// TRANSFER_COMMIT_BATCH files (not one per file), and for TransferMode::Move the source
// removals are deferred into ONE remove_media_batch after the destination commit — so a
// bulk transfer costs O(batches) durable commits, like the import queue, instead of
// O(files). Destination durability always strictly precedes any source mutation: a crash
// mid-transfer loses at most the uncommitted batch (still in the source) or leaves
// already-committed files briefly in both vaults (recoverable duplicates), never neither.
// With `prog.progress != nullptr`: total is set to filenames.size() up front (skipped
// when `prog.set_total` is false — for callers like vault combine that manage a larger
// progress total themselves), done is bumped after each file, and the loop stops early
// when progress->cancel is set — files copied so far are flushed and (for Move) removed
// from the source, a clean partial. A destination commit failure fails every file of
// that batch and stops the transfer. Returns {committed, failed}; failed files are left
// in the source.
struct TransferProgress {
    OpProgress* progress  = nullptr;
    bool        set_total = true;
};
[[nodiscard]] TransferTally transfer_images(Vault& src, std::string_view src_gallery,
                                            const std::vector<std::string>& filenames,
                                            Vault& dst, std::string_view dst_gallery,
                                            TransferMode mode, TransferProgress prog = {});

// Slash-paths of every gallery in `v` that may legally accept media (images or videos)
// — holds no sub-galleries, including "" (root) when root holds no sub-galleries. Used
// to populate the transfer dialog's destination-gallery list. Empty while locked.
[[nodiscard]] std::vector<std::string> image_target_galleries(const Vault& v);

// Transfer a whole gallery subtree from `src` (the gallery at `src_gallery`) into
// `dst` under `dst_parent`, keeping the gallery's own name. Per-file tolerance: copy
// every file; failures are recorded in `tally` (when given) rather than aborting.
// Batched (Phase 69): recreated galleries and copied files become durable in ONE
// destination commit per TRANSFER_COMMIT_BATCH files; for TransferMode::Move the source
// removals are deferred into ONE remove_media_batch after the destination commit (a
// crash mid-Move leaves already-committed files briefly in both vaults — recoverable
// duplicates, never a loss). After all files, if no cancel and
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
// A cancel stops cleanly: files copied so far are flushed and (for Move) removed from
// the source in the finish batch, the rest stay only in src — no duplicates, nothing
// lost. Pruning is
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
