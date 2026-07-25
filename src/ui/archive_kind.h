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

} // namespace ui
