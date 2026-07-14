#pragma once

#include "ui/zip_import.h"
#include "ui/zip_plan.h"

#include <filesystem>
#include <string_view>

namespace vault {
class Vault;
}

namespace ui {

// Import the supported media from `archive_path` (.7z/.rar/.tar/.tar.gz/
// .tar.xz) into `v`, reusing the exact same ZipPlan planner (build_zip_plan)
// and mirror/append semantics as import_zip — only the decompression backend
// differs (ui::ArchiveReader/libarchive instead of miniz). Decompresses each
// entry into mlock'd memory only (invariant #1), never to disk.
//
// Always declared regardless of whether this binary was built with
// OSV_VENDORED_ARCHIVE: on a build without it, returns a graceful "not
// supported" outcome (ok=false, a user-facing error) instead of being
// unavailable at the call site — mirrors ui::VideoPlayback's poster-only
// fallback on non-AV builds, so GalleryGrid never needs its own #ifdef.
[[nodiscard]] // `password_protected` (Phase 35) is true only for a zip/cbz the caller
// already knows is encrypted (ui::zip_is_encrypted at pick time). When true,
// a one-entry verification probe runs before any vault write; a wrong or
// missing `password` returns needs_password=true with nothing written.
// Always false/empty for plain 7z/RAR/TAR imports — those readers fail
// unconditionally on encrypted content regardless of any passphrase, so no
// verification step is offered for them.
[[nodiscard]] ZipImportOutcome import_archive(vault::Vault&                v,
                                              const std::filesystem::path& archive_path,
                                              ZipDest                      dest,
                                              std::string_view             base_gallery,
                                              std::string_view             new_gallery_name,
                                              ZipConflictPolicy            policy,
                                              ImportProgress*              progress = nullptr,
                                              bool                         password_protected = false,
                                              std::string_view             password = {});

// Import a `.cbr`/`.cb7`/`.cbt` comic archive as a single leaf gallery of
// pages (build_cbz_plan), mirroring import_cbz's semantics over the
// libarchive backend. Same non-OSV_VENDORED_ARCHIVE fallback as import_archive.
[[nodiscard]] [[nodiscard]] ZipImportOutcome import_archive_cbz(vault::Vault&                v,
                                                  const std::filesystem::path& archive_path,
                                                  std::string_view             base_gallery,
                                                  std::string_view             gallery_name,
                                                  ImportProgress*              progress = nullptr,
                                                  bool                         password_protected = false,
                                                  std::string_view             password = {});

}  // namespace ui
