#pragma once

#include "ui/zip_plan.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vault {
class Vault;
}

namespace ui {

struct ZipImportOutcome {
    bool                     ok = false;
    bool                     needs_resolution = false;
    int                      imported = 0;
    int                      skipped = 0;
    std::vector<std::string> mixed_dirs;
    std::string              error;
};

// Import the supported media from `zip_path` into `v`. Decompresses each entry
// into mlock'd memory only (invariant #1). With ZipDest::NewGallery the tree is
// mirrored under base_gallery/new_gallery_name; with Append every file is
// flattened into base_gallery. If policy==AskUser and the archive has mixed
// folders, returns needs_resolution=true and writes nothing.
[[nodiscard]] ZipImportOutcome import_zip(vault::Vault&                v,
                                          const std::filesystem::path& zip_path,
                                          ZipDest                      dest,
                                          std::string_view             base_gallery,
                                          std::string_view             new_gallery_name,
                                          ZipConflictPolicy            policy);

}  // namespace ui
