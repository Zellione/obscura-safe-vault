#pragma once

#include "ui/meta_json.h"
#include "ui/zip_plan.h"
#include "vault/op_progress.h"

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
    bool                     needs_password   = false;  // encrypted zip/cbz: wrong/missing password,
                                                         // nothing written (Phase 35)
    bool                     cancelled = false;     // user pressed Esc during import (Phase 26)
    int                      imported = 0;
    int                      skipped = 0;
    std::string              error;
};

// Shared, thread-safe progress for a running import (Phase 24). Now an alias of
// the generic vault::OpProgress (Phase 25) so ZipImportJob and FileOpJob share
// one progress/cancel type. The importer stores `total` (files to place) before
// the first page and bumps `done` after each; a poller on another thread reads
// them for a progress bar. Setting `cancel` cooperatively stops the placement
// loop between pages — the pages stored so far remain (append-only vault), so a
// cancel is a clean partial import, never a corrupt one.
using ImportProgress = vault::OpProgress;

// Import the supported media from `zip_path` into `v`. Decompresses each entry
// into mlock'd memory only (invariant #1). The archive tree is mirrored 1:1
// under base_gallery/new_gallery_name (see build_zip_plan).
// `progress` (optional) is updated as files are placed and polled for cancel;
// pass nullptr for a plain synchronous import with no progress/cancel.
[[nodiscard]] ZipImportOutcome import_zip(vault::Vault&                v,
                                          const std::filesystem::path& zip_path,
                                          std::string_view             base_gallery,
                                          std::string_view             new_gallery_name,
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

// Read an archive's optional top-level meta.json without importing anything
// (Phase 27). The UI uses this when a zip/cbz is picked to prefill the
// gallery-name popup via meta_gallery_name — the name the user confirms is then
// authoritative for the import (never silently overridden by the title).
// Missing/malformed meta.json or an unreadable archive → empty ArchiveMeta.
[[nodiscard]] ArchiveMeta peek_archive_meta(const std::filesystem::path& archive_path);

// True if `zip_path` has at least one entry using ZIP encryption (any
// flavor — this only detects the flag, not which kind). miniz can still list
// an encrypted zip's central directory (only content extraction fails), so
// this is a cheap peek, mirroring peek_archive_meta's cost/timing. False on
// any open failure (never blocks a normal import attempt) or a fully
// unencrypted archive.
[[nodiscard]] bool zip_is_encrypted(const std::filesystem::path& zip_path);

}  // namespace ui
