#pragma once

// Phase 53: the executor that makes recursive import reachable from the queue.
//
// Composes the pieces: read the archive, confirm its kind by magic, then run
// walk_archive over the real backends. Mirrors import_zip's sink convention
// exactly — the plan root is `new_gallery_name` and the hooks strip it before
// talking to the MediaSink — so this drops into process_archive_task in place
// of the flat importer without changing where anything lands.

#include "ui/zip_import.h"   // ZipImportOutcome, ImportProgress

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace ui {

class MediaSink;

// `sink_root` is the prefix to strip from absolute gallery paths before handing
// them to `sink`, and it exists because the two sinks disagree about where the
// gallery name lives:
//
//   DirectVaultSink(v, base, name)  base_ ALREADY includes `name`
//                                   -> pass sink_root = name (strip it)
//   ImportQueue::StagingSink(v, dest) base_ is `dest` alone, no name
//                                   -> pass sink_root = "" (strip nothing)
//
// Getting this wrong silently drops or duplicates a whole gallery level, so it
// is a required argument rather than something inferred.
// Import an already-assembled archive buffer. Used for a multi-volume set,
// whose bytes are the joined (or merged) volumes and therefore exist only in
// memory — writing them to a temp file just to re-read them would be a
// pointless full copy of the archive to disk.
//
// `display_name` is only used to confirm the kind by extension + magic.
[[nodiscard]] ZipImportOutcome import_archive_bytes_recursive(
    MediaSink&               sink,
    std::span<const uint8_t> bytes,
    std::string_view         display_name,
    std::string_view         new_gallery_name,
    std::string_view         sink_root,
    ImportProgress*          progress = nullptr,
    std::string_view         password = {});

[[nodiscard]] ZipImportOutcome import_archive_recursive(
    MediaSink&                   sink,
    const std::filesystem::path& archive_path,
    std::string_view             new_gallery_name,
    std::string_view             sink_root,
    ImportProgress*              progress = nullptr,
    std::string_view             password = {});

} // namespace ui
