#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "vault/vault.h"   // Vault, VaultResult

namespace vault {

// Move one image from `src` (gallery `src_gallery`, file `filename`) into `dst`'s
// `dst_gallery`, keeping the filename. Reads the source plaintext into an mlock'd
// SecureBytes, re-encrypts it into `dst` via add_image (which regenerates the
// thumbnail + metadata), then removes it from `src`. `dst` is committed before
// `src` is mutated, so a crash in between leaves the image in BOTH vaults (a
// recoverable duplicate) rather than losing it. Plaintext lives only in the locked
// buffer (invariant #1).
//
// Returns the first failing step's result, leaving the source untouched if the
// destination add fails:
//   NotFound      - src image, src gallery, or dst gallery missing
//   AlreadyExists - dst_gallery already holds `filename`
//   InvalidArg    - dst_gallery is not a leaf that can accept images
//   AuthFailed / IoError / Locked / CryptoError - propagated from the underlying ops
[[nodiscard]] VaultResult move_image(Vault& src, std::string_view src_gallery,
                                     std::string_view filename,
                                     Vault& dst, std::string_view dst_gallery);

// Slash-paths of every gallery in `v` that may legally accept images (holds no
// sub-galleries), including "" (root) when root holds no sub-galleries. Used to
// populate the transfer dialog's destination-gallery list. Empty while locked.
[[nodiscard]] std::vector<std::string> image_target_galleries(const Vault& v);

// Move a whole gallery subtree from `src` (the gallery at `src_gallery`) into `dst`
// under `dst_parent`, keeping the gallery's own name. Copy-then-delete: the subtree
// is recreated + every descendant image copied into `dst` FIRST, then the source
// subtree is removed — so a crash mid-move leaves a recoverable duplicate, never a
// loss. Image plaintext lives only in mlock'd memory (invariant #1).
//   NotFound      - src gallery missing / not a gallery
//   AlreadyExists - dst_parent already holds a child of the same name
//   InvalidArg    - dst_parent cannot hold a sub-gallery (it holds images), or
//                   src_gallery is the root ("")
//   AuthFailed / IoError / Locked - propagated; the source is left intact if any
//                   copy fails before the final remove.
[[nodiscard]] VaultResult move_gallery(Vault& src, std::string_view src_gallery,
                                       Vault& dst, std::string_view dst_parent);

// Slash-paths of every gallery in `v` that may legally accept a SUB-gallery (i.e.
// holds no images), including "" (root) when root holds no images. Empty while
// locked. Used to populate the transfer dialog when the source is a gallery.
[[nodiscard]] std::vector<std::string> gallery_target_parents(const Vault& v);

} // namespace vault
