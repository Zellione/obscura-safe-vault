#include "ui/file_op_job.h"

#include <algorithm>
#include <format>
#include <utility>

#include "platform/paths.h"
#include "ui/export.h"
#include "ui/meta_format.h"
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

// Format a transfer outcome from a tally (shared by the image-list + gallery paths).
FileOpOutcome transfer_outcome(vault::TransferMode mode, int done, int failed, int total,
                               bool cancelled, const std::string& label)
{
    const char* verb        = (mode == vault::TransferMode::Copy) ? "Copied" : "Moved";
    const char* cancel_verb = (mode == vault::TransferMode::Copy) ? "Copy"   : "Move";
    FileOpOutcome oc;
    oc.ok        = true;
    oc.cancelled = cancelled;
    oc.done      = done;
    oc.failed    = failed;
    oc.total     = total;
    oc.kind      = FileOpKind::Transfer;
    if (cancelled)
        oc.status = std::format("{} cancelled — {} of {} to {}", cancel_verb, done, total, label);
    else
        oc.status = std::format("{} {} of {} to {}", verb, done, total, label);
    if (failed > 0) oc.status += std::format(" ({} failed)", failed);
    return oc;
}

// Import one file into `base_gallery`, dispatching by extension. Returns
// true on success; on failure writes the bare filename (no path, no
// secrets) into `*fail_name` for the capped failed-name list.
bool import_one(vault::Vault& v, const std::string& base_gallery,
                const std::filesystem::path& path, std::string* fail_name)
{
    using enum vault::VaultResult;
    // filename() already strips the directory, but the leaf itself can still be
    // a name the vault refuses (reserved device name, trailing dot, ...), and a
    // rejected name would fail the import for no good reason. Repair, don't reject.
    const std::string fname = vault::sanitize_node_name(path.filename().string());
    const auto bytes = platform::read_file(path);
    if (!bytes) { *fail_name = fname; return false; }

    const bool video = is_video_filename(fname);
    if (const vault::VaultResult r = video ? v.add_video(base_gallery, *bytes, fname)
                                           : v.add_image(base_gallery, *bytes, fname);
        r == Ok)
        return true;
    *fail_name = fname;
    return false;
}

// Format the "(K failed: name1, name2, name3, +N more)" suffix for a finished
// import's status line. Capped at 3 names — the footer renders as one
// unwrapped text line (src/ui/gallery_grid.cpp's draw_footer_status), so an
// uncapped list would run off the window. Extracted from run_import to keep
// its nesting within the cpp:S134 depth cap.
std::string format_failed_suffix(const std::vector<std::string>& failed_names)
{
    constexpr size_t MAX_SHOWN = 3;
    std::string names;
    for (size_t i = 0; i < std::min(failed_names.size(), MAX_SHOWN); ++i) {
        if (i > 0) names += ", ";
        names += failed_names[i];
    }
    if (failed_names.size() > MAX_SHOWN)
        names += std::format(", +{} more", failed_names.size() - MAX_SHOWN);
    return std::format(" ({} failed: {})", failed_names.size(), names);
}

FileOpOutcome run_import(vault::Vault& v, const std::string& base_gallery,
                         const std::vector<std::filesystem::path>& files,
                         vault::OpProgress& progress)
{
    progress.total.store(static_cast<int>(files.size()));

    int imported = 0;
    std::vector<std::string> failed_names;
    for (const auto& path : files) {
        if (progress.cancel.load()) break;
        if (std::string fail_name; import_one(v, base_gallery, path, &fail_name)) ++imported;
        else                                                                     failed_names.push_back(fail_name);
        progress.done.fetch_add(1);
    }

    FileOpOutcome oc;
    oc.ok        = true;   // per-file failures are counted, not a hard failure
    oc.cancelled = progress.cancel.load();
    oc.done      = imported;
    oc.failed    = static_cast<int>(failed_names.size());
    oc.total     = static_cast<int>(files.size());
    oc.kind      = FileOpKind::Import;

    if (oc.cancelled) {
        oc.status = std::format("Import cancelled — {} imported", imported);
    } else {
        oc.status = std::format("Imported {}", count_noun(imported, "file"));
        if (!failed_names.empty()) oc.status += format_failed_suffix(failed_names);
    }
    return oc;
}

FileOpOutcome run_compact(vault::Vault& v, vault::OpProgress& progress)
{
    if (progress.cancel.load()) {
        FileOpOutcome oc;
        oc.ok = true; oc.cancelled = true;
        oc.kind = FileOpKind::Compact;
        oc.status = "Compaction cancelled.";
        return oc;
    }

    // compact() owns progress.total and progress.done; it scans all chunks and
    // writes them to a temp file with progress updates before atomically
    // replacing the original.
    const vault::VaultResult r = v.compact(&progress);
    FileOpOutcome oc;
    oc.kind = FileOpKind::Compact;
    if (r == vault::VaultResult::Ok) {
        oc.ok = true;
        oc.done = progress.done.load();
        oc.total = progress.total.load();
        oc.status = "Vault compacted successfully.";
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

bool FileOpJob::start_transfer_galleries(vault::Vault& src, std::vector<std::string> src_paths,
                                         vault::Vault& dst, std::string dst_parent,
                                         vault::TransferMode mode, std::string label)
{
    return launch(FileOpKind::Transfer,
                  [this, &src, src_paths = std::move(src_paths), &dst,
                   dst_parent = std::move(dst_parent), mode, label = std::move(label)]() {
        const vault::TransferTally t = vault::transfer_galleries(
            src, src_paths, dst, dst_parent, mode, &progress_);
        return transfer_outcome(mode, t.done, t.failed, static_cast<int>(src_paths.size()),
                                progress_.cancel.load(), label);
    });
}

bool FileOpJob::start_compact(vault::Vault& v)
{
    return launch(FileOpKind::Compact, [this, &v]() { return run_compact(v, progress_); });
}

bool FileOpJob::start_import(vault::Vault& v, std::string base_gallery,
                             std::vector<std::filesystem::path> files)
{
    return launch(FileOpKind::Import,
                  [this, &v, base_gallery = std::move(base_gallery),
                   files = std::move(files)]() {
        return run_import(v, base_gallery, files, progress_);
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
