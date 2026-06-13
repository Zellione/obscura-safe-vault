#pragma once

// Phase 10 — Export: deliberately extract decrypted images out of the vault to
// ordinary files on disk.
//
// SECURITY NOTE: this module is the ONE place that intentionally violates
// invariant #1 ("no plaintext to disk"). It is gated by explicit per-export
// consent (ExportConsent::Confirm), only ever writes the current explicit
// selection (never a whole-tree dump), and the decrypted bytes live only in an
// mlock'd SecureBytes right up to the write() call and are crypto_wipe'd
// immediately afterwards. Thumbnails are never exported.
//
// SDL-free by design so the write/collision logic stays headlessly testable;
// the consent dialog and folder picker live in the UI/platform layers.

#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>

#include "vault/vault.h"

namespace ui {

// The user's answer to the per-export consent dialog. Cancel is the default.
enum class ExportConsent { Cancel, Confirm };

struct ExportSummary {
    int written = 0;
    int failed  = 0;
};

// Resolve a non-colliding output path. Returns `dir/filename` if `exists`
// reports it free; otherwise appends " (1)", " (2)", ... before the extension
// until a free name is found. Pure — `exists(path) -> bool` injects the
// filesystem probe (templated on the callable to avoid a std::function alloc).
template <class Exists>
[[nodiscard]] std::filesystem::path unique_export_path(
    const std::filesystem::path& dir, std::string_view filename, Exists&& exists)
{
    std::filesystem::path candidate = dir / filename;
    if (!exists(candidate)) return candidate;

    // Split "name.ext" so the counter goes before the extension: "name (1).ext".
    const std::filesystem::path base(filename);
    const std::string stem = base.stem().string();
    const std::string ext  = base.extension().string();
    for (int n = 1;; ++n) {
        candidate = dir / std::format("{} ({}){}", stem, n, ext);
        if (!exists(candidate)) return candidate;
    }
}

// Decrypt `node`'s ORIGINAL stored bytes into `scratch` (mlock'd) and write them
// verbatim to `out_path`, then crypto_wipe `scratch`. `scratch` is reused/resized
// by the caller across a batch. Returns InvalidArg for a non-image node,
// whatever read_image returns on failure, or IoError if the file write fails.
[[nodiscard]] vault::VaultResult export_one_image(const vault::Vault&            vault,
                                                  const vault::IndexNode&        node,
                                                  const std::filesystem::path&   out_path,
                                                  crypto::SecureBytes&           scratch);

// Export every image in `images` to `dest_dir`, collision-suffixing names. A
// no-op returning {0,0} unless `consent == Confirm`. Non-image / failed nodes
// increment `failed` and are skipped. Thumbnails are never written.
[[nodiscard]] ExportSummary export_images(const vault::Vault&                          vault,
                                          std::span<const vault::IndexNode* const>     images,
                                          const std::filesystem::path&                 dest_dir,
                                          ExportConsent                                consent);

} // namespace ui
