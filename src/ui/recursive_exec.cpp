#include "ui/recursive_exec.h"

#include "ui/archive_kind.h"
#include "ui/import_common.h"
#include "ui/media_sink.h"
#include "ui/recursive_hooks.h"
#include "ui/recursive_import.h"

#include <vector>

namespace ui {

ZipImportOutcome import_archive_recursive(MediaSink&                   sink,
                                          const std::filesystem::path& archive_path,
                                          std::string_view             new_gallery_name,
                                          std::string_view             sink_root,
                                          ImportProgress*              progress,
                                          std::string_view             password)
{
    ZipImportOutcome out;

    std::vector<uint8_t> bytes;
    if (!read_whole_file(archive_path, bytes)) {
        out.error = "Could not read archive";
        return out;
    }

    // The extension got us this far; the magic decides. A file whose name lies
    // must fail here rather than be handed to a backend that cannot read it.
    const ArchiveKind kind = detect_archive_kind(archive_path.filename().string(), bytes);
    if (kind == ArchiveKind::None) {
        out.error = "Not a recognised archive";
        return out;
    }

    RecursiveHooks hooks = make_recursive_hooks(sink, sink_root, password);

    // Progress: the total is NOT knowable up front, because nested archives are
    // only discovered as their parent is decompressed. `done` still advances per
    // placed file so the existing bar moves; the growing-total + "expanding"
    // presentation is a separate piece of work.
    if (progress != nullptr) {
        auto place = hooks.place_media;
        hooks.place_media = [place, progress](std::string_view gallery, std::string_view filename,
                                              std::span<const uint8_t> data) {
            const bool ok = place(gallery, filename, data);
            progress->done.fetch_add(1);
            return ok;
        };
    }

    const RecursiveTally tally = walk_archive(bytes, kind, new_gallery_name, hooks);

    out.ok        = true;
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
