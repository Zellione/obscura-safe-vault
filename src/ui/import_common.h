#pragma once

// Small internal helpers shared by the two archive-import backends
// (zip_import.cpp/miniz and archive_import.cpp/libarchive) so the identical
// whole-file-read and VaultResult-tally logic isn't duplicated between them.
// Not part of either backend's public API (zip_import.h/archive_import.h);
// only their .cpp files include this.

#include "ui/zip_import.h"
#include "vault/vault.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ui {

// Read a whole file into memory as raw bytes. The archive is the user's own
// on-disk file; only the *decompressed* entry bytes are sensitive (those go
// to mlock'd SecureBytes elsewhere). Returns false if missing or empty.
[[nodiscard]] bool read_whole_file(const std::filesystem::path& path, std::vector<uint8_t>& out);

// Tally a Vault::add_image/add_video result into `out` (imported/skipped/
// first error), shared by both backends' per-entry import loop.
void tally_import_result(vault::VaultResult r, const std::string& filename, ZipImportOutcome& out);

} // namespace ui
