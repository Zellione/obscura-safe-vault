#pragma once

// The .osv vault: the public API tying the header, chunk store, and index tree
// into a usable encrypted gallery.
//
// Lifecycle / state machine:
//   create()  -> file written, vault UNLOCKED (master key in hand)
//   open()    -> header parsed, vault LOCKED (no master key yet)
//   unlock()  -> KEK derived, master key unwrapped, index loaded -> UNLOCKED
//   lock()    -> master key wiped, index dropped -> LOCKED (re-unlockable)
//
// Security invariants upheld here (see CLAUDE.md):
//   * The master key lives only in an mlock'd SecureBuffer, wiped on lock/destroy.
//   * Decrypted image data is returned in an mlock'd SecureBytes — never an
//     unlocked heap buffer, never a temp file (invariant #1).
//   * Index writes are crash-safe: the new index blob is appended and fsync'd,
//     then the inactive header slot is pointed at it, then `active_slot` is
//     flipped in a final one-byte-significant header write (double buffering).

#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "crypto/crypto.h"
#include "crypto/kdf.h"
#include "crypto/secure_mem.h"

#include "header.h"
#include "index.h"

namespace vault {

enum class VaultResult {
    Ok,
    IoError,        // open/read/write/fsync failure
    BadFormat,      // not a valid .osv / unparseable header or index
    AuthFailed,     // wrong password/keyfile, or tampered/undecryptable chunk
    Locked,         // operation needs an unlocked vault
    NotFound,       // gallery / image path does not exist
    AlreadyExists,  // gallery / image name already taken
    InvalidArg,     // bad argument or leaf-invariant violation
    CryptoError,    // RNG / KDF failure
};

class Vault {
public:
    // Auto-compaction gates (remove_image): rewrite the vault only when at
    // least this much is reclaimable AND the waste is at least a quarter of
    // the file — rewriting everything to reclaim a few KiB costs more I/O
    // than it returns.
    static constexpr uint64_t AUTO_COMPACT_MIN_WASTE   = 256 * 1024;
    static constexpr uint64_t AUTO_COMPACT_WASTE_RATIO = 4;  // waste >= size/4

    Vault() = default;
    ~Vault();

    Vault(const Vault&)            = delete;
    Vault& operator=(const Vault&) = delete;
    Vault(Vault&& other) noexcept;
    Vault& operator=(Vault&& other) noexcept;

    // Create a brand-new vault at `path` (truncating any existing file). On
    // success `out` is returned UNLOCKED and ready to use. `keyfile` may be empty.
    [[nodiscard]] static VaultResult create(const std::string&       path,
                                            std::span<const uint8_t>  password,
                                            std::span<const uint8_t>  keyfile,
                                            const crypto::KdfParams&  params,
                                            Vault&                    out);

    // Open an existing vault and parse its header. Returns a LOCKED vault.
    [[nodiscard]] static VaultResult open(const std::string& path, Vault& out);

    // Derive the KEK, unwrap the master key, and load the index. On success the
    // vault is UNLOCKED. Returns AuthFailed for a wrong password/keyfile.
    [[nodiscard]] VaultResult unlock(std::span<const uint8_t> password,
                                     std::span<const uint8_t> keyfile);

    // Wipe the master key and drop the in-memory index. The file stays open and
    // the vault can be unlocked again.
    void lock() noexcept;

    // Re-wrap the master key under a KEK derived from the new credentials
    // (fresh salt + nonce). The old credentials are verified first; AuthFailed
    // leaves the vault untouched. Data chunks are NOT re-encrypted — the master
    // key itself never changes. Works on any open vault, locked or unlocked,
    // and preserves the lock state.
    [[nodiscard]] VaultResult change_password(std::span<const uint8_t> old_password,
                                              std::span<const uint8_t> old_keyfile,
                                              std::span<const uint8_t> new_password,
                                              std::span<const uint8_t> new_keyfile);

    [[nodiscard]] bool is_unlocked() const noexcept { return unlocked_; }

    // Create a gallery at `gallery_path` (slash-separated), creating intermediate
    // galleries as needed. Fails with InvalidArg if any path segment is an image
    // or would violate the leaf invariant (a gallery holding images can't gain a
    // sub-gallery). AlreadyExists if the gallery already exists.
    [[nodiscard]] VaultResult create_gallery(std::string_view gallery_path);

    // Encrypt and store `file_data` as an image named `filename` in the leaf
    // gallery `gallery_path` (root is ""). InvalidArg if the target holds
    // sub-galleries. AlreadyExists if the name is taken.
    [[nodiscard]] VaultResult add_image(std::string_view         gallery_path,
                                        std::span<const uint8_t> file_data,
                                        std::string_view         filename);

    // Decrypt the image described by `node` into mlock'd memory. AuthFailed if the
    // chunk fails authentication (tamper / corruption).
    [[nodiscard]] VaultResult read_image(const IndexNode& node, crypto::SecureBytes& out) const;

    // Decrypt the thumbnail for `node` into mlock'd memory. NotFound if the node
    // has no stored thumbnail (thumb_length == 0). AuthFailed on tamper/corruption.
    [[nodiscard]] VaultResult read_thumbnail(const IndexNode& node, crypto::SecureBytes& out) const;

    // Remove an image from the index (its chunk is orphaned, reclaimed by Phase 7
    // compaction). NotFound if the image does not exist.
    [[nodiscard]] VaultResult remove_image(std::string_view gallery_path,
                                           std::string_view filename);

    // Immediate children of `gallery_path`. Pointers are valid until the next
    // mutating call. Empty if the path is missing or not a gallery.
    [[nodiscard]] std::vector<const IndexNode*> list(std::string_view gallery_path) const;

    // Reclaimable bytes: the part of the data region not referenced by any live
    // image/thumbnail chunk or the active index blob (orphaned chunks from
    // deletes plus superseded index blobs). 0 while locked — the index is
    // needed to know what is live.
    [[nodiscard]] uint64_t wasted_bytes() const;

    // Rewrite the vault to hold only live data: copy live chunks verbatim into
    // a new file, write a fresh index + header, fsync, then atomically rename
    // over the original. Reclaims wasted_bytes(). Invalidates all IndexNode
    // pointers previously returned by list().
    [[nodiscard]] VaultResult compact();

private:
    // Persist the in-memory index with the crash-safe double-buffer swap.
    [[nodiscard]] VaultResult commit_index();
    // Write the current header_ to offset 0 and fsync.
    [[nodiscard]] bool write_header();
    // Resolve a slash-separated gallery path to a node (nullptr if missing /
    // not a gallery). Empty path resolves to the root.
    [[nodiscard]] IndexNode*       find_gallery(std::string_view path);
    [[nodiscard]] const IndexNode* find_gallery(std::string_view path) const;

    void reset() noexcept;  // close file, wipe key, clear state

    std::string                            path_;
    std::FILE*                             fp_ = nullptr;
    Header                                 header_;
    bool                                   unlocked_ = false;
    crypto::SecureBuffer<crypto::KEY_SIZE> master_key_;
    IndexNode                              root_ = IndexNode::gallery("");
};

} // namespace vault
