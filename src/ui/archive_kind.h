#pragma once

// Phase 53: archive-kind classification for recursive import.
//
// Extension proposes, magic bytes confirm. A file whose extension claims an
// archive but whose magic disagrees is reported as `None` and skipped silently
// by the planner — a formatting surprise inside someone else's archive is not a
// critical failure (same philosophy as Phase 34's reader).
//
// Pure: no miniz, no libarchive, no vault, no SDL.

#include <cstdint>
#include <span>
#include <string_view>

namespace ui {

enum class ArchiveKind : uint8_t { None, Zip, Cbz, SevenZip, Rar, Tar, TarGz };

// Classify `filename` + its leading bytes. `bytes` may be short or empty; the
// function never reads past its end. Returns `None` unless the extension names
// a supported archive AND the magic bytes agree.
[[nodiscard]] ArchiveKind detect_archive_kind(std::string_view       filename,
                                              std::span<const uint8_t> bytes);

// Extension-only test: does `name` CLAIM to be an archive?
//
// The planner (`build_zip_plan`) sees entry names but not entry bytes, so it
// cannot magic-check. It uses this to route a candidate into `ZipPlan::nested`;
// the orchestrator then extracts the bytes and calls `detect_archive_kind` to
// confirm. A candidate whose magic disagrees is counted as unsupported there,
// not here — which is why this predicate is deliberately permissive.
[[nodiscard]] bool is_archive_name(std::string_view name);

// The archive extension `name` ends with (lower-cased, leading dot included),
// or "" if none. `is_archive_name` is defined in terms of this, so the two can
// never disagree about what counts as an archive.
[[nodiscard]] std::string_view archive_extension_of(std::string_view name);

} // namespace ui
