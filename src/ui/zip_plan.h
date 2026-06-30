#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ui {

enum class ZipDest { NewGallery, Append };
enum class ZipConflictPolicy { AskUser, FlattenMixed, SkipMixed };

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
    std::vector<std::string>  mixed_dirs;           // archive dirs that violate the leaf invariant
    int                       skipped_unsupported = 0;
    bool                      needs_resolution = false;
};

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

// Build a placement plan from raw archive entries. See zip_plan.cpp for the
// mirror/append/mixed-folder rules. Pure: no miniz, no vault, no SDL.
[[nodiscard]] ZipPlan build_zip_plan(const std::vector<ZipEntry>& entries,
                                     ZipDest                      dest,
                                     std::string_view             base_gallery,
                                     std::string_view             new_gallery_name,
                                     ZipConflictPolicy            policy);

} // namespace ui
