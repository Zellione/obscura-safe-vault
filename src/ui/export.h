#pragma once

// Phase 10 — Export: deliberately extract decrypted images out of the vault to
// ordinary files on disk.
//
// SECURITY NOTE: this module is the ONE place that intentionally violates
// invariant #1 ("no plaintext to disk"). It is gated by explicit per-export
// consent (ExportConsent::Confirm), only ever writes the current explicit
// selection (including selected gallery subtrees), and decrypted bytes live in an
// mlock'd SecureBytes right up to the write() call and are crypto_wipe'd
// immediately afterwards. Thumbnails are never exported.
//
// Because it is the one path that turns vault bytes back into real files, it is
// also the one place a hostile vault could aim at: IndexNode::name is opaque
// deserialised data (src/vault/index.cpp), and a name like "../../.bashrc" or
// "/etc/cron.d/x" would steer the write out of the folder the user picked. Two
// layers stop that, in the order Sonar's cpp:S2083 prescribes (normalize, then
// validate, then use): the name is run through vault::sanitize_node_name, and
// the resulting component is claimed ATOMICALLY by
// platform::create_new_file_within (Phase 98 / OSV-AUD-005) — an exclusive
// no-follow, containment-enforced create that doubles as the collision test and
// returns an already-open handle, so the write happens strictly inside the
// picked folder and a path that was merely "checked earlier" is never reopened.
// Phase 120 pins directories with platform::OutputDirectory throughout a
// recursive export. Display paths are never reopened or used to authorize writes.
//
// SDL-free by design so the write/collision logic stays headlessly testable;
// the consent dialog and folder picker live in the UI/platform layers.

#include <filesystem>
#include <span>
#include <string>

#include "platform/atomic_file.h"
#include "platform/path_utf8.h"
#include "vault/op_progress.h"
#include "vault/vault.h"

namespace ui {

// The user's answer to the per-export consent dialog. Cancel is the default.
enum class ExportConsent { Cancel, Confirm };

// Counts filesystem entries: original files and gallery directories.
struct ExportSummary {
    int written = 0;
    int failed  = 0;
};

// True iff `candidate` resolves to a location strictly inside `dest_dir`.
//
// Phase 98 (OSV-AUD-005) retired the old check-then-truncating-open path, so
// this is a diagnostic for tests (weakly_canonical both operands, containment
// via lexically_relative). Production exports rely on pinned directory
// descriptors. A relative
// path that is empty, is ".", or contains any ".." component means the
// candidate escaped. Fails closed: a filesystem error resolving either operand
// returns false.
[[nodiscard]] bool export_path_within(const std::filesystem::path& dest_dir,
                                      const std::filesystem::path& candidate);

// Decrypt `node`'s ORIGINAL stored bytes into `scratch` (mlock'd) and write them
// verbatim to the ALREADY-OPEN `out` handle, then crypto_wipe `scratch`.
// `scratch` is reused/resized by the caller across a batch. Handles images AND
// videos (Phase 53 made videos selectable); a video's chunks are concatenated
// into the same mlock'd buffer.
//
// Phase 98 (OSV-AUD-005): `out` comes from platform::create_new_file_within —
// an atomically claimed, symlink-safe handle. export_one_media NEVER reopens a
// path ("a path that was merely checked earlier" is the race the audit found):
// the caller does not build a path, probe it, and hand it in, it hands in the
// open handle. Ownership of `out.fp` transfers here (closed by this call).
//
// Returns InvalidArg for anything else — notably a gallery, which has no stored
// bytes of its own — whatever the read returns on failure, or IoError if the
// file write, flush, or close fails. The scratch buffer is wiped on EVERY path.
[[nodiscard]] vault::VaultResult export_one_media(const vault::Vault&      vault,
                                                  const vault::IndexNode&  node,
                                                  platform::NewOutputFile  out,
                                                  crypto::SecureBytes&     scratch);

// Export the explicit selection: originals as files, galleries as directories
// containing their full subtree (including empty galleries). Overlapping or
// duplicate selections are exported once. Every name is sanitized and claimed
// exclusively; existing folders are never merged. A no-op unless Confirm.
// Progress counts files and directories, including skipped failed subtrees.
// Cancel stops between entries and leaves completed output in place.
[[nodiscard]] ExportSummary export_images(const vault::Vault&                          vault,
                                          std::span<const vault::IndexNode* const>     images,
                                          const std::filesystem::path&                 dest_dir,
                                          ExportConsent                                consent,
                                          vault::OpProgress*                           progress = nullptr);

} // namespace ui
