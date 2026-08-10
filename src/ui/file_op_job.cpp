#include "ui/file_op_job.h"

#include <algorithm>
#include <format>
#include <utility>

#include "platform/paths.h"
#include "ui/export.h"
#include "ui/failure_list_dialog.h"
#include "ui/meta_format.h"
#include "vault/combine.h"
#include "vault/safe_name.h"
#include "vault/vault.h"

namespace ui {

namespace {

// "1 image" vs "3 images" — shared plural helper for the outcome wording.
std::string count_noun(int n, const char* noun)
{
    return std::format("{} {}{}", n, noun, n == 1 ? "" : "s");
}

// The three worker bodies as free helpers (keeps the start_* lambdas one-liners,
// under the cpp:S1188 lambda-length cap). Each takes the job's progress by ref.

FileOpOutcome run_export(const vault::Vault& v, const std::vector<const vault::IndexNode*>& nodes,
                         const std::filesystem::path& dest, const std::string& label,
                         vault::OpProgress& progress)
{
    const ExportSummary sum = export_images(v, nodes, dest, ExportConsent::Confirm, &progress);

    FileOpOutcome oc;
    oc.ok        = true;   // export always "succeeds"; per-file failures are counted
    oc.cancelled = progress.cancel.load();
    oc.done      = sum.written;
    oc.failed    = sum.failed;
    oc.total     = progress.total.load();
    oc.kind      = FileOpKind::Export;
    if (oc.cancelled)
        oc.status = std::format("Export cancelled — {} written to {}", oc.done, label);
    else
        oc.status = std::format("Exported {} to {}", count_noun(oc.done, "image"), label);
    if (oc.failed > 0) oc.status += std::format(" ({} failed)", oc.failed);
    return oc;
}

FileOpOutcome run_delete(vault::Vault& v, const std::string& base, const std::string& name,
                         bool is_gallery, int item_total, vault::OpProgress& progress)
{
    progress.total.store(item_total);

    FileOpOutcome oc;
    oc.kind  = FileOpKind::Delete;
    oc.total = item_total;
    if (progress.cancel.load()) {   // cancelled before it ran — nothing removed
        oc.ok = true; oc.cancelled = true; oc.status = "Delete cancelled";
        return oc;
    }

    const std::string full = base.empty() ? name : base + "/" + name;
    if (const vault::VaultResult r = is_gallery ? v.remove_gallery(full) : v.remove_image(base, name);
        r == vault::VaultResult::Ok) {
        progress.done.store(item_total);
        oc.ok = true; oc.done = item_total;
        oc.status = std::format("Deleted {}", name);
    } else {
        oc.error = std::format("Could not delete {}", name);
    }
    return oc;
}

// Counts of one finished transfer, bundled for S107.
struct TransferCounts {
    int done = 0;
    int failed = 0;
    int skipped = 0;
    int total = 0;
};

// Gallery transfer parameters, bundled for S107.
struct GalleryTransferSpec {
    std::string              src_gallery;
    std::string              dst_parent;
    vault::TransferMode      mode;
    vault::CollisionPolicy   policy;
    std::string              label;
};

// Format a transfer outcome from counts (shared by the image-list + gallery paths).
FileOpOutcome transfer_outcome(vault::TransferMode mode, TransferCounts c, bool cancelled,
                               const std::string& label,
                               std::vector<vault::TransferFailure> failures = {})
{
    const char* verb        = (mode == vault::TransferMode::Copy) ? "Copied" : "Moved";
    const char* cancel_verb = (mode == vault::TransferMode::Copy) ? "Copy"   : "Move";
    FileOpOutcome oc;
    oc.ok        = true;
    oc.cancelled = cancelled;
    oc.done      = c.done;
    oc.failed    = c.failed;
    oc.skipped   = c.skipped;
    oc.total     = c.total;
    oc.kind      = FileOpKind::Transfer;
    oc.failures  = std::move(failures);
    if (cancelled)
        oc.status = std::format("{} cancelled — {} of {} to {}", cancel_verb, c.done, c.total, label);
    else
        oc.status = std::format("{} {} of {} to {}", verb, c.done, c.total, label);
    if (c.failed > 0)  oc.status += std::format(" ({} failed)", c.failed);
    if (c.skipped > 0) oc.status += std::format(" ({} skipped)", c.skipped);
    return oc;
}

FileOpOutcome combine_outcome(vault::VaultResult r, const vault::CombineTally& tally,
                              bool cancelled, const std::string& label)
{
    FileOpOutcome oc;
    oc.kind = FileOpKind::Transfer;
    if (r != vault::VaultResult::Ok) {
        oc.error = "Combine failed.";
        return oc;
    }
    oc.ok        = true;
    oc.cancelled = cancelled;
    oc.done      = tally.media_moved;
    oc.failed    = tally.media_skipped;
    oc.total     = tally.media_moved + tally.media_skipped;
    oc.status    = cancelled
        ? std::format("Combine cancelled — {} moved, {} skipped, into {}",
                      tally.media_moved, tally.media_skipped, label)
        : std::format("Combined into {} — {} moved, {} skipped", label, tally.media_moved,
                      tally.media_skipped);
    return oc;
}


FileOpOutcome run_gallery_transfer(vault::Vault& src, vault::Vault& dst,
                                   const GalleryTransferSpec& spec, vault::OpProgress& progress)
{
    vault::TransferTally tally;
    const vault::VaultResult r =
        vault::transfer_gallery(src, spec.src_gallery, dst, spec.dst_parent, spec.mode,
                                {.progress = &progress, .tally = &tally, .policy = spec.policy});
    const bool cancelled = progress.cancel.load();

    if (r != vault::VaultResult::Ok) {
        FileOpOutcome oc;
        oc.kind = FileOpKind::Transfer;
        oc.error = "Move/Copy failed - " +
                   transfer_failure_reason(r, vault::TransferFailure::Stage::Write);
        return oc;
    }
    // Gallery transfer is one logical item; report media progress as the counts.
    const int total = progress.total.load();
    const int done  = progress.done.load();
    return transfer_outcome(spec.mode,
                            {.done = done, .failed = tally.failed, .skipped = tally.skipped,
                             .total = total},
                            cancelled, spec.label, std::move(tally.failures));
}

FileOpOutcome run_compact(vault::Vault& v, vault::OpProgress& progress)
{
    if (progress.cancel.load()) {
        FileOpOutcome oc;
        oc.ok = true; oc.cancelled = true;
        oc.kind = FileOpKind::Compact;
        oc.status = "Compact cancelled — progress kept.";
        return oc;
    }

    // compact() owns progress.total and progress.done; it scans all chunks and
    // packs them in-place, with progress updates before atomic index batch-commits.
    const uint64_t before = vault::vault_file_bytes(v);
    const vault::VaultResult r = v.compact(&progress);
    FileOpOutcome oc;
    oc.kind = FileOpKind::Compact;
    if (r == vault::VaultResult::Ok) {
        oc.ok = true;
        oc.done = progress.done.load();
        oc.total = progress.total.load();
        const uint64_t after = vault::vault_file_bytes(v);
        oc.status = std::format("Vault compacted — {:.1f} MiB reclaimed.",
                                before > after ? double(before - after) / (1 << 20) : 0.0);
    } else {
        oc.error = std::format("Compaction failed: {:d}.", std::to_underlying(r));
    }
    return oc;
}

} // namespace

FileOpJob::~FileOpJob()
{
    progress_.cancel.store(true);   // stop the loop promptly on teardown
    if (thread_.joinable()) thread_.join();
}

bool FileOpJob::launch(FileOpKind kind, std::function<FileOpOutcome()> work)
{
    if (active_.load()) return false;   // one op at a time

    progress_.total.store(0);
    progress_.done.store(0);
    progress_.cancel.store(false);
    outcome_ = {};
    kind_ = kind;
    done_.store(false);
    active_.store(true);

    thread_ = OpWorkerThread([this, work = std::move(work)]() mutable {
        outcome_ = work();
        done_.store(true);   // release: happens-before the joining thread's read
    });
    return true;
}

bool FileOpJob::start_export(const vault::Vault& v, std::vector<const vault::IndexNode*> nodes,
                             std::filesystem::path dest_dir, std::string label)
{
    return launch(FileOpKind::Export,
                  [this, &v, nodes = std::move(nodes), dest_dir = std::move(dest_dir),
                   label = std::move(label)]() {
        return run_export(v, nodes, dest_dir, label, progress_);
    });
}

bool FileOpJob::start_delete(vault::Vault& v, std::string base, std::string name,
                             bool is_gallery, int item_total)
{
    return launch(FileOpKind::Delete,
                  [this, &v, base = std::move(base), name = std::move(name),
                   is_gallery, item_total]() {
        return run_delete(v, base, name, is_gallery, item_total, progress_);
    });
}

bool FileOpJob::start_transfer_images(vault::Vault& src, std::string src_gallery,
                                      std::vector<std::string> filenames,
                                      vault::Vault& dst, std::string dst_gallery,
                                      vault::TransferMode mode, std::string label)
{
    return launch(FileOpKind::Transfer,
                  [this, &src, src_gallery = std::move(src_gallery),
                   filenames = std::move(filenames), &dst, dst_gallery = std::move(dst_gallery),
                   mode, label = std::move(label)]() {
        vault::TransferTally t =
            vault::transfer_images(src, src_gallery, filenames, dst, dst_gallery, mode,
                                   {.progress = &progress_});
        return transfer_outcome(mode,
                                {.done = t.done, .failed = t.failed, .skipped = t.skipped,
                                 .total = static_cast<int>(filenames.size())},
                                progress_.cancel.load(), label, std::move(t.failures));
    });
}

bool FileOpJob::start_transfer_gallery(vault::Vault& src, std::string src_gallery,
                                       vault::Vault& dst, std::string dst_parent,
                                       vault::TransferMode mode, vault::CollisionPolicy policy,
                                       std::string label)
{
    return launch(FileOpKind::Transfer,
                  [this, &src, &dst, spec = GalleryTransferSpec{
                       .src_gallery = std::move(src_gallery), .dst_parent = std::move(dst_parent),
                       .mode = mode, .policy = policy, .label = std::move(label)}]() {
        return run_gallery_transfer(src, dst, spec, progress_);
    });
}

bool FileOpJob::start_transfer_galleries(vault::Vault& src, std::vector<std::string> src_paths,
                                         vault::Vault& dst, std::string dst_parent,
                                         vault::TransferMode mode, vault::CollisionPolicy policy,
                                         std::string label)
{
    return launch(FileOpKind::Transfer,
                  [this, &src, src_paths = std::move(src_paths), &dst,
                   dst_parent = std::move(dst_parent), mode, policy, label = std::move(label)]() {
        vault::TransferTally t = vault::transfer_galleries(
            src, src_paths, dst, dst_parent, mode, &progress_, policy);
        return transfer_outcome(mode,
                                {.done = t.done, .failed = t.failed, .skipped = t.skipped,
                                 .total = static_cast<int>(src_paths.size())},
                                progress_.cancel.load(), label, std::move(t.failures));
    });
}

bool FileOpJob::start_transfer_media_grouped(vault::Vault& src, std::vector<ParentGroup> groups,
                                             vault::Vault& dst, std::string dst_gallery,
                                             vault::TransferMode mode, std::string label)
{
    return start_transfer_collection(src, {std::move(groups), {}}, dst, std::move(dst_gallery),
                                     mode, vault::CollisionPolicy::Fail, std::move(label));
}

namespace {

// Worker body of start_transfer_collection, a named function to keep the
// launch lambda within the S1188 line budget. Media first, then subtrees; a
// cancel between phases is a clean partial (each vault::transfer_* item is a
// committed unit).
vault::TransferTally run_collection_transfer(vault::Vault& src, const CollectionTransferSpec& spec,
                                             vault::Vault& dst, const std::string& dst_target,
                                             vault::TransferMode mode, vault::OpProgress& progress,
                                             vault::CollisionPolicy policy)
{
    vault::TransferTally sum;
    for (const ParentGroup& g : spec.groups) {
        if (progress.cancel.load()) break;
        vault::TransferTally t =
            vault::transfer_images(src, g.parent, g.names, dst, dst_target, mode,
                                   {.progress = &progress});
        sum.done   += t.done;
        sum.failed += t.failed;
        sum.skipped += t.skipped;
        sum.failures.insert(sum.failures.end(), std::make_move_iterator(t.failures.begin()),
                            std::make_move_iterator(t.failures.end()));
    }
    if (!spec.gallery_paths.empty() && !progress.cancel.load()) {
        vault::TransferTally t =
            vault::transfer_galleries(src, spec.gallery_paths, dst, dst_target, mode, &progress, policy);
        sum.done   += t.done;
        sum.failed += t.failed;
        sum.skipped += t.skipped;
        sum.failures.insert(sum.failures.end(), std::make_move_iterator(t.failures.begin()),
                            std::make_move_iterator(t.failures.end()));
    }
    return sum;
}

}  // namespace

bool FileOpJob::start_transfer_collection(vault::Vault& src, CollectionTransferSpec spec,
                                          vault::Vault& dst, std::string dst_target,
                                          vault::TransferMode mode, vault::CollisionPolicy policy,
                                          std::string label)
{
    return launch(FileOpKind::Transfer,
                  [this, &src, spec = std::move(spec), &dst, dst_target = std::move(dst_target),
                   mode, policy, label = std::move(label)]() {
        vault::TransferTally t = run_collection_transfer(src, spec, dst, dst_target, mode,
                                                         progress_, policy);
        return transfer_outcome(mode,
                                {.done = t.done, .failed = t.failed, .skipped = t.skipped,
                                 .total = t.done + t.failed + t.skipped},
                                progress_.cancel.load(), label, std::move(t.failures));
    });
}

bool FileOpJob::start_combine(vault::Vault& src, std::string src_gallery,
                              vault::Vault& dst, std::string dst_gallery, std::string label)
{
    return launch(FileOpKind::Transfer,
                  [this, &src, src_gallery = std::move(src_gallery), &dst,
                   dst_gallery = std::move(dst_gallery), label = std::move(label)]() {
        vault::CombineTally tally;
        const vault::VaultResult r =
            vault::combine_galleries(src, src_gallery, dst, dst_gallery, tally, &progress_);
        return combine_outcome(r, tally, progress_.cancel.load(), label);
    });
}

bool FileOpJob::start_compact(vault::Vault& v)
{
    return launch(FileOpKind::Compact, [this, &v]() { return run_compact(v, progress_); });
}

std::optional<FileOpOutcome> FileOpJob::take_outcome()
{
    if (!active_.load() || !done_.load()) return std::nullopt;
    if (thread_.joinable()) thread_.join();   // synchronises outcome_ into this thread
    active_.store(false);
    return outcome_;
}

} // namespace ui
