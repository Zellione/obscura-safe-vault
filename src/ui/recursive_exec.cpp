#include "ui/recursive_exec.h"

#include "ui/archive_kind.h"
#include "ui/import_common.h"
#include "ui/media_sink.h"
#include "ui/recursive_hooks.h"
#include "ui/recursive_import.h"

#include "platform/path_utf8.h"

#include <vector>

namespace ui {

ZipImportOutcome import_archive_recursive(MediaSink&                   sink,
                                          const std::filesystem::path& archive_path,
                                          std::string_view             new_gallery_name,
                                          std::string_view             sink_root,
                                          ImportProgress*              progress,
                                          std::string_view             password)
{
    std::vector<uint8_t> bytes;
    if (!read_whole_file(archive_path, bytes)) {
        ZipImportOutcome out;
        out.error = "Could not read archive";
        return out;
    }
    return import_archive_bytes_recursive(sink, bytes, platform::path_to_utf8(archive_path.filename()),
                                          new_gallery_name, sink_root, progress, password);
}

ZipImportOutcome import_archive_bytes_recursive(MediaSink& sink, std::span<const uint8_t> bytes,
                                                std::string_view display_name,
                                                std::string_view new_gallery_name,
                                                std::string_view sink_root,
                                                ImportProgress*  progress,
                                                std::string_view password)
{
    ZipImportOutcome out;

    // The extension got us this far; the magic decides. A file whose name lies
    // must fail here rather than be handed to a backend that cannot read it.
    const ArchiveKind kind = detect_archive_kind(display_name, bytes);
    if (kind == ArchiveKind::None) {
        out.error = "Not a recognised archive";
        return out;
    }

    RecursiveHooks hooks = make_recursive_hooks(sink, sink_root, password);

    // Nested password failures remain per-branch skips. A failure in the picked
    // archive itself must be reported as a failed import, including failures
    // after its (possibly unencrypted) header was successfully listed.
    ArchiveReadError root_error = ArchiveReadError::None;
    const auto capture_error = [&](std::span<const uint8_t> source, bool ok) {
        if (!ok && source.data() == bytes.data() && source.size() == bytes.size() && hooks.read_error)
            root_error = hooks.read_error();
        return ok;
    };
    auto list = hooks.list_entries;
    hooks.list_entries = [list, capture_error](auto source, auto type, auto& entries) {
        return capture_error(source, list(source, type, entries));
    };
    auto extract = hooks.extract_entry;
    hooks.extract_entry = [extract, capture_error](auto source, auto type, auto index, auto& output) {
        return capture_error(source, extract(source, type, index, output));
    };

    auto cancelled = hooks.cancelled;
    hooks.cancelled = [cancelled, &root_error] {
        return root_error != ArchiveReadError::None || (cancelled && cancelled());
    };

    // Progress: the total is NOT knowable up front, because nested archives are
    // only discovered as their parent is decompressed. `done` still advances per
    // placed file so the existing bar moves; the growing-total + "expanding"
    // presentation is a separate piece of work.
    if (progress != nullptr) {
        progress->expanding.store(true);
        progress->total.store(0);
        hooks.note_planned = [progress](int n) { progress->total.fetch_add(n); };

        auto place = hooks.place_media;
        hooks.place_media = [place, progress](std::string_view gallery, std::string_view filename,
                                              std::span<const uint8_t> data) {
            const bool ok = place(gallery, filename, data);
            progress->done.fetch_add(1);
            return ok;
        };
    }

    const RecursiveTally tally = walk_archive(bytes, kind, new_gallery_name, hooks);
    if (progress != nullptr) {
        // The tree is fully explored: the total is final, so the bar stops
        // being a lower bound.
        progress->expanding.store(false);
    }

    // A corrupt root archive fails the task (Phase 68) — before, it was folded
    // into `skipped` and the task rendered as "Done, 0 imported, 1 skipped".
    out.ok = !tally.root_unreadable && root_error == ArchiveReadError::None;
    if (!out.ok) {
        out.error = root_error == ArchiveReadError::None ? "Archive is corrupt or unreadable"
                                                        : std::string(archive_read_error_text(root_error));
        out.needs_password = root_error == ArchiveReadError::PasswordRequired ||
                             root_error == ArchiveReadError::WrongPassword;
    }
    out.imported  = tally.media_placed;
    out.cancelled = sink.cancelled();
    // Everything that did not become a file in the vault, for one honest count:
    // unsupported entries, entries whose name lied about being an archive,
    // archives that could not be read, and branches a guard cut off.
    out.skipped = tally.skipped_unsupported + tally.not_an_archive + tally.unreadable +
                  tally.depth_capped + tally.budget_stopped + tally.encrypted_skipped;
    return out;
}

} // namespace ui
