#include "ui/file_op_job.h"

#include <format>
#include <utility>

#include "ui/export.h"
#include "vault/vault.h"

namespace ui {

namespace {

// "1 image" vs "3 images" — shared plural helper for the outcome wording.
std::string count_noun(int n, const char* noun)
{
    return std::format("{} {}{}", n, noun, n == 1 ? "" : "s");
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
        const ExportSummary sum =
            export_images(v, nodes, dest_dir, ExportConsent::Confirm, &progress_);

        FileOpOutcome oc;
        oc.ok        = true;   // export always "succeeds"; per-file failures are counted
        oc.cancelled = progress_.cancel.load();
        oc.done      = sum.written;
        oc.failed    = sum.failed;
        oc.total     = progress_.total.load();
        oc.kind      = FileOpKind::Export;
        oc.status = oc.cancelled
            ? std::format("Export cancelled — {} written to {}", oc.done, label)
            : std::format("Exported {} to {}", count_noun(oc.done, "image"), label);
        if (oc.failed > 0) oc.status += std::format(" ({} failed)", oc.failed);
        return oc;
    });
}

bool FileOpJob::start_delete(vault::Vault& v, std::string base, std::string name,
                             bool is_gallery, int item_total)
{
    return launch(FileOpKind::Delete,
                  [this, &v, base = std::move(base), name = std::move(name),
                   is_gallery, item_total]() {
        progress_.total.store(item_total);

        FileOpOutcome oc;
        oc.kind  = FileOpKind::Delete;
        oc.total = item_total;
        if (progress_.cancel.load()) {   // cancelled before it ran — nothing removed
            oc.ok = true; oc.cancelled = true; oc.status = "Delete cancelled";
            return oc;
        }

        const vault::VaultResult r =
            is_gallery ? v.remove_gallery(base.empty() ? name : base + "/" + name)
                       : v.remove_image(base, name);
        if (r == vault::VaultResult::Ok) {
            progress_.done.store(item_total);
            oc.ok = true; oc.done = item_total;
            oc.status = std::format("Deleted {}", name);
        } else {
            oc.error = std::format("Could not delete {}", name);
        }
        return oc;
    });
}

// Shared tail of the two transfer starters: format the outcome from a tally.
namespace {
FileOpOutcome transfer_outcome(vault::TransferMode mode, int done, int failed, int total,
                               bool cancelled, const std::string& label)
{
    const char* verb = (mode == vault::TransferMode::Copy) ? "Copied" : "Moved";
    FileOpOutcome oc;
    oc.ok        = true;
    oc.cancelled = cancelled;
    oc.done      = done;
    oc.failed    = failed;
    oc.total     = total;
    oc.kind      = FileOpKind::Transfer;
    oc.status = cancelled
        ? std::format("{} cancelled — {} of {} to {}",
                      (mode == vault::TransferMode::Copy) ? "Copy" : "Move", done, total, label)
        : std::format("{} {} of {} to {}", verb, done, total, label);
    if (failed > 0) oc.status += std::format(" ({} failed)", failed);
    return oc;
}
} // namespace

bool FileOpJob::start_transfer_images(vault::Vault& src, std::string src_gallery,
                                      std::vector<std::string> filenames,
                                      vault::Vault& dst, std::string dst_gallery,
                                      vault::TransferMode mode, std::string label)
{
    return launch(FileOpKind::Transfer,
                  [this, &src, src_gallery = std::move(src_gallery),
                   filenames = std::move(filenames), &dst, dst_gallery = std::move(dst_gallery),
                   mode, label = std::move(label)]() {
        const vault::TransferTally t =
            vault::transfer_images(src, src_gallery, filenames, dst, dst_gallery, mode, &progress_);
        return transfer_outcome(mode, t.done, t.failed, static_cast<int>(filenames.size()),
                                progress_.cancel.load(), label);
    });
}

bool FileOpJob::start_transfer_gallery(vault::Vault& src, std::string src_gallery,
                                       vault::Vault& dst, std::string dst_parent,
                                       vault::TransferMode mode, std::string label)
{
    return launch(FileOpKind::Transfer,
                  [this, &src, src_gallery = std::move(src_gallery), &dst,
                   dst_parent = std::move(dst_parent), mode, label = std::move(label)]() {
        const vault::VaultResult r =
            vault::transfer_gallery(src, src_gallery, dst, dst_parent, mode, &progress_);
        const bool cancelled = progress_.cancel.load();

        if (r != vault::VaultResult::Ok) {
            FileOpOutcome oc;
            oc.kind = FileOpKind::Transfer;
            oc.error = "Move/Copy failed.";
            return oc;
        }
        // Gallery transfer is one logical item; report media progress as the counts.
        const int total = progress_.total.load();
        const int done  = progress_.done.load();
        return transfer_outcome(mode, done, 0, total, cancelled, label);
    });
}

std::optional<FileOpOutcome> FileOpJob::take_outcome()
{
    if (!active_.load() || !done_.load()) return std::nullopt;
    if (thread_.joinable()) thread_.join();   // synchronises outcome_ into this thread
    active_.store(false);
    return outcome_;
}

} // namespace ui
