#pragma once

// Phase 53: multi-volume (split) archive detection.
//
// Pure: takes the picked filename plus a listing of sibling filenames and
// works entirely on strings — no filesystem access, so it is unit-testable
// without touching disk. The caller supplies the listing and later opens the
// files.
//
// Observed volume naming (real output from the tools, not convention):
//   NumericSuffix  archive.7z.001 … .103   (7z, 3-digit, starts at 001)
//                  archive.tar.00  … .54   (split -d, 2-digit, starts at 00)
//   SpannedZip     archive.z01 … .z51 + archive.zip   (zip -s; .zip is LAST)
//   RarPart        name.part1.rar / .part01.rar / .part0001.rar (all three
//                  padding widths occur in libarchive's own fixtures)
//   RarOld         name.rar + name.r00 + name.r01 …   (.rar is volume ONE)

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

enum class VolumeStyle : uint8_t {
    None,           // not part of a multi-volume set
    NumericSuffix,  // .001/.002… or .00/.01… — raw byte splits, concatenate
    RarPart,        // .partN.rar — libarchive archive_read_open_filenames
    RarOld,         // .rar + .r00 + .r01 — libarchive archive_read_open_filenames
    SpannedZip,     // .z01… + .zip — needs the spanned-ZIP merger
};

struct VolumeSet {
    VolumeStyle              style = VolumeStyle::None;
    std::string              stem;     // shared base name, extension(s) removed
    std::vector<std::string> volumes;  // filenames in VOLUME order (not sorted)
    // Ordinals absent from an otherwise-contiguous run, in the set's own
    // numbering. Non-empty means the set is incomplete and must not be
    // imported — a gap yields a corrupt gallery, not a partial one.
    std::vector<int>         missing;
};

// Identify the multi-volume set `picked` belongs to, given the filenames
// alongside it (`siblings` may or may not include `picked` itself).
//
// Returns `style == None` when `picked` is an ordinary single-file archive.
// That is the common case and must stay cheap: a lone `photos.zip` with no
// `.z01` beside it is NOT a broken spanned set.
[[nodiscard]] VolumeSet detect_volume_set(std::string_view             picked,
                                          std::span<const std::string> siblings);

} // namespace ui
