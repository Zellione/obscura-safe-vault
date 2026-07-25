#pragma once

// Phase 53: turning a detected multi-volume set into something importable, and
// describing it for the confirm dialog.
//
// Two halves, separated so the decision logic is testable without disk:
//   summarize_volume_set  pure — what the dialog shows, and whether the set may
//                         be imported at all
//   assemble_volume_set   reads the volumes and produces either one buffer or a
//                         list of paths, per the style's assembly route

#include "ui/volume_set.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ui {

// What the confirm dialog renders. `can_import` false means a gap was found:
// importing an incomplete set yields a corrupt gallery, not a partial one, so
// it is refused rather than attempted.
struct VolumeSetSummary {
    std::string              heading;
    std::vector<std::string> volume_lines;   // one per volume, in set order
    std::string              warning;        // gap description, empty if complete
    bool                     can_import = false;
};

[[nodiscard]] VolumeSetSummary summarize_volume_set(const VolumeSet& set);

// The bytes (or paths) an importer needs. Exactly one of `bytes`/`paths` is
// populated, chosen by `assembly`:
//   Concatenate / SpannedZipMerge -> bytes
//   FileOriented                  -> paths (each RAR volume has its own header)
struct AssembledVolumes {
    VolumeAssembly                     assembly = VolumeAssembly::None;
    std::vector<uint8_t>               bytes;
    std::vector<std::filesystem::path> paths;
    std::string                        error;   // non-empty => nothing usable

    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

// Read and assemble `set`, whose volumes live in `dir`. Refuses a set with
// gaps. Every path is run through platform::normalize_user_path first —
// ArchiveReader::open_files deliberately does not normalise, leaving it to the
// boundary where externally-supplied paths enter (invariant 6).
[[nodiscard]] AssembledVolumes assemble_volume_set(const VolumeSet&             set,
                                                   const std::filesystem::path& dir);

} // namespace ui
