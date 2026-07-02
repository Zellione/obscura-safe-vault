#pragma once

#include "ui/zip_plan.h"

#include <atomic>
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

// Shared, thread-safe progress for a running import (Phase 24). The importer
// stores `total` (the number of files to place) before the first page and bumps
// `done` after each; a poller on another thread reads them for a progress bar.
// Setting `cancel` cooperatively stops the placement loop between pages — the
// pages stored so far remain (the vault is append-only), so a cancel is a clean
// partial import, never a corrupt one.
struct ImportProgress {
    std::atomic<int>  total{0};
    std::atomic<int>  done{0};
    std::atomic<bool> cancel{false};
};

// Import the supported media from `zip_path` into `v`. Decompresses each entry
// into mlock'd memory only (invariant #1). With ZipDest::NewGallery the tree is
// mirrored under base_gallery/new_gallery_name; with Append every file is
// flattened into base_gallery. If policy==AskUser and the archive has mixed
// folders, returns needs_resolution=true and writes nothing.
// `progress` (optional) is updated as files are placed and polled for cancel;
// pass nullptr for a plain synchronous import with no progress/cancel.
[[nodiscard]] ZipImportOutcome import_zip(vault::Vault&                v,
                                          const std::filesystem::path& zip_path,
                                          ZipDest                      dest,
                                          std::string_view             base_gallery,
                                          std::string_view             new_gallery_name,
                                          ZipConflictPolicy            policy,
                                          ImportProgress*              progress = nullptr);

// Import a `.cbz` comic archive as a single leaf gallery `base_gallery/gallery_name`
// of pages (build_cbz_plan). Reuses the import_zip miniz/SecureBytes path:
// every image entry is decompressed into mlock'd memory only (invariant #1),
// flattening internal subfolders, in natural reading order. Non-image entries
// are skipped + counted. Never extracts to disk.
[[nodiscard]] ZipImportOutcome import_cbz(vault::Vault&                v,
                                          const std::filesystem::path& cbz_path,
                                          std::string_view             base_gallery,
                                          std::string_view             gallery_name,
                                          ImportProgress*              progress = nullptr);

}  // namespace ui
