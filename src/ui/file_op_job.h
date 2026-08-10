#pragma once

#include "ui/parent_group.h"  // ParentGroup (Phase 68 grouped transfer)
#include "vault/op_progress.h"
#include "vault/transfer.h"   // vault::TransferMode, vault::TransferFailure, vault::CollisionPolicy

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace vault {
class Vault;
struct IndexNode;
}

namespace ui {

// One collection-screen selection: per-parent media groups + gallery subtrees.
struct CollectionTransferSpec {
    std::vector<ParentGroup> groups;
    std::vector<std::string> gallery_paths;
};

using OpWorkerThread = std::jthread;

// Which bulk operation ran (drives the outcome wording).
enum class FileOpKind { None, Export, Delete, Transfer, Compact };

struct FileOpOutcome {
    bool        ok        = false;   // the op ran to completion (or a clean cancel)
    bool        cancelled = false;   // stopped early on a user cancel (partial result)
    int         done      = 0;       // items committed to the destination / removed
    int         failed    = 0;       // items that failed (skipped)
    int         skipped   = 0;       // destination-name collisions (not attempted)
    int         total     = 0;       // items attempted
    FileOpKind  kind      = FileOpKind::None;
    std::string status;              // human-facing summary (no secrets)
    std::string error;               // set when ok == false
    std::vector<vault::TransferFailure> failures;   // per-item transfer failures (Phase 67)
};

// Runs a bulk file operation — export, delete, or move/copy (transfer) — on a
// background thread so a large gallery never freezes the UI (Phase 25).
// An OpProgress drives an "N / M" bar and a cooperative cancel.
//
// Threading contract: the vault's file handle is single-threaded — while active(),
// the worker thread is the ONLY thread touching the vault(s). The owning screen
// MUST NOT read any involved vault (no thumbnail decrypt, no listing) until
// take_outcome() returns; it only polls progress and draws a modal. A cross-vault
// transfer touches BOTH vaults, so both must be kept alive and untouched by the UI
// for the job's life. cancel() stops between items; each item is a committed,
// crash-safe unit, so a cancel is a clean partial.
class FileOpJob {
public:
    FileOpJob() = default;
    ~FileOpJob();

    FileOpJob(const FileOpJob&)            = delete;
    FileOpJob& operator=(const FileOpJob&) = delete;

    // Export image `nodes` (pointers into `v`'s live index; export is read-only so
    // the index is not mutated) to `dest_dir`. `label` is a display name for the
    // destination (shown in the outcome). `v` is kept alive by the caller.
    bool start_export(const vault::Vault& v, std::vector<const vault::IndexNode*> nodes,
                      std::filesystem::path dest_dir, std::string label);

    // Delete the media (is_gallery=false) or gallery subtree (is_gallery=true) named
    // `name` under gallery `base` in `v`. `item_total` is the pre-counted item count
    // for the bar; removal is one atomic index op (done jumps to total on success).
    bool start_delete(vault::Vault& v, std::string base, std::string name,
                      bool is_gallery, int item_total);

    // Delete every node named by `node_paths` (full slash-paths; media and
    // gallery subtrees alike) in ONE vault::remove_nodes_batch commit (Phase 74).
    // `item_total` is the pre-counted progress-bar unit count (media + galleries,
    // all depths — BatchDeleteSummary::item_total); done jumps to total on the
    // single atomic commit.
    bool start_delete_batch(vault::Vault& v, std::vector<std::string> node_paths,
                            int item_total);

    // Move/Copy a list of media (`filenames` in src/src_gallery) into dst/dst_gallery
    // (dst may be the same vault as src). `label` names the destination vault.
    bool start_transfer_images(vault::Vault& src, std::string src_gallery,
                               std::vector<std::string> filenames,
                               vault::Vault& dst, std::string dst_gallery,
                               vault::TransferMode mode, std::string label);

    // Move/Copy a whole gallery subtree from src/src_gallery under dst/dst_parent.
    bool start_transfer_gallery(vault::Vault& src, std::string src_gallery,
                                vault::Vault& dst, std::string dst_parent,
                                vault::TransferMode mode, vault::CollisionPolicy policy,
                                std::string label);

    // Move/Copy a LIST of whole gallery subtrees (`src_paths`) into dst/dst_parent
    // (Phase 44 Part 3) — the bulk sibling of start_transfer_gallery, for a
    // multi-selection of gallery tiles.
    bool start_transfer_galleries(vault::Vault& src, std::vector<std::string> src_paths,
                                  vault::Vault& dst, std::string dst_parent,
                                  vault::TransferMode mode, vault::CollisionPolicy policy,
                                  std::string label);

    // Move/Copy media grouped by SOURCE parent (Phase 68): a collection-screen
    // selection spans galleries, so each group runs vault::transfer_images from
    // its own parent; tallies merge into one outcome.
    bool start_transfer_media_grouped(vault::Vault& src, std::vector<ParentGroup> groups,
                                      vault::Vault& dst, std::string dst_gallery,
                                      vault::TransferMode mode, std::string label);

    // The full collection-screen selection in one run (Phase 68): per-parent
    // media groups PLUS whole gallery subtrees, all landing in/under dst_target.
    // Tallies merge into one outcome.
    bool start_transfer_collection(vault::Vault& src, CollectionTransferSpec spec,
                                   vault::Vault& dst, std::string dst_target,
                                   vault::TransferMode mode, vault::CollisionPolicy policy,
                                   std::string label);

    // Combine src/src_gallery into dst/dst_gallery — recursive merge, deletes
    // src_gallery once empty (Phase 44 Part 4). `label` names the destination
    // vault for the outcome message.
    bool start_combine(vault::Vault& src, std::string src_gallery,
                       vault::Vault& dst, std::string dst_gallery, std::string label);

    // Compact the vault in-place, reclaiming wasted_bytes(). Runs on background thread
    // with progress tracking and cooperative cancel.
    bool start_compact(vault::Vault& v);

    // True from a start_*() call until take_outcome() has collected the result.
    [[nodiscard]] bool active() const noexcept { return active_.load(); }

    // Live counters for a progress bar (valid while active()).
    [[nodiscard]] int total() const noexcept { return progress_.total.load(); }
    [[nodiscard]] int done()  const noexcept { return progress_.done.load(); }

    // The kind of op in flight (for choosing modal wording while it runs).
    [[nodiscard]] FileOpKind kind() const noexcept { return kind_; }

    // Request a cooperative stop; items committed so far remain.
    void cancel() noexcept { progress_.cancel.store(true); }

    // If the worker has finished, join it and hand back the outcome exactly once;
    // returns nullopt while still running or when there is nothing to collect.
    [[nodiscard]] std::optional<FileOpOutcome> take_outcome();

private:
    // Reset counters and spawn `work` (which returns the outcome) on the worker
    // thread. Shared by every start_*. Returns false if a job is already running.
    bool launch(FileOpKind kind, std::function<FileOpOutcome()> work);

    vault::OpProgress progress_;
    std::atomic<bool> active_{false};   // job in flight or finished-but-uncollected
    std::atomic<bool> done_{false};     // worker set this at the end
    FileOpKind        kind_ = FileOpKind::None;
    FileOpOutcome     outcome_;         // written by worker; read after join
    OpWorkerThread    thread_;
};

} // namespace ui
