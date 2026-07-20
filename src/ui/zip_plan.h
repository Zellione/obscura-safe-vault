#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

struct ZipEntry {
    std::string path;     // '/'-separated; trailing '/' or is_dir => directory
    bool        is_dir = false;
};

struct ZipPlacement {
    std::string gallery_path;   // vault gallery to receive the file
    std::string filename;       // basename stored in the vault
    size_t      entry_index;    // index back into the caller's entry list
};

struct ZipPlan {
    std::vector<std::string>  galleries;            // create in this order (parents first)
    std::vector<ZipPlacement> placements;
    int                       skipped_unsupported = 0;
};

// Index of the archive's top-level `meta.json` (case-insensitive, files only),
// if any (Phase 27). The importer consumes it for the gallery title/tags; the
// planners exclude it silently (neither placed nor counted as skipped).
[[nodiscard]] std::optional<size_t> find_meta_entry(const std::vector<ZipEntry>& entries);

// True if `name`'s extension is a supported image or video container.
[[nodiscard]] bool is_supported_media_name(std::string_view name);

// True if `name`'s extension is a supported *image* (not video). Used by the
// CBZ path, which imports pages only.
[[nodiscard]] bool is_supported_image_name(std::string_view name);

// Build a fixed CBZ plan: one leaf gallery `base_gallery/gallery_name` holding
// every supported image entry (videos/other skipped + counted), flattening any
// internal subfolders, ordered in natural reading order over the full entry
// path. No mixed-folder resolution. Pure: no miniz, no vault, no SDL.
[[nodiscard]] ZipPlan build_cbz_plan(const std::vector<ZipEntry>& entries,
                                     std::string_view             base_gallery,
                                     std::string_view             gallery_name);

// Build a placement plan from raw archive entries, mirroring the archive tree
// 1:1 under base_gallery/new_gallery_name. A directory holding both media and
// subdirectories maps directly onto a mixed gallery (Phase 46), so no conflict
// resolution is ever required. Pure: no miniz, no vault, no SDL.
[[nodiscard]] ZipPlan build_zip_plan(const std::vector<ZipEntry>& entries,
                                     std::string_view             base_gallery,
                                     std::string_view             new_gallery_name);

} // namespace ui
