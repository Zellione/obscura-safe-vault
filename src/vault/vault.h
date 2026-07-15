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
#include "op_progress.h"

namespace media { class VideoSource; }

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

// Video chunk size (1 MiB plaintext split). Used by add_video; tests may pass a
// smaller value to force multi-chunk paths with tiny fixtures.
inline constexpr uint32_t VIDEO_CHUNK_SIZE = 1u << 20;

enum class SearchScope { Images, Galleries, Both };

// A node matched by search. `path` is the full slash-path to the node (including
// its own name); `effective_tags` = the node's own tags unioned with all ancestor
// gallery tags (cascade, computed at read time, case-insensitively de-duplicated).
struct SearchHit {
    std::string              path;
    bool                     is_gallery = false;
    std::string              name;
    std::vector<std::string> effective_tags;
    const IndexNode*         node = nullptr;  // valid until the next mutating call
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

    // Move-only: declaring the move operations implicitly deletes the copy ones,
    // so no explicit `= delete` for the copy ctor/assignment is needed.
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

    // The media layer's VideoSource class borrows fp_ + master_key_ to stream a
    // stored video's chunks on demand. friend keeps Vault's public surface flat
    // (cpp:S1448 method cap). VideoSource's static factory open() is defined in
    // src/media/video_source.cpp.
    friend class ::media::VideoSource;

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

    // Import a video container: detect its type by magic bytes, split the plaintext
    // into `chunk_size` AEAD chunks, and store a Type::Video node. dims/duration/codec/
    // poster stay empty until the decoder (PR4) fills them. `chunk_size` defaults to
    // VIDEO_CHUNK_SIZE; a smaller value (tests) forces the multi-chunk path. Returns
    // InvalidArg for a non-MP4/MKV container.
    [[nodiscard]] VaultResult add_video(std::string_view         gallery_path,
                                        std::span<const uint8_t> file_data,
                                        std::string_view         filename,
                                        uint32_t                 chunk_size = VIDEO_CHUNK_SIZE);

    // Decrypt + concatenate all of a video node's chunks into mlock'd memory.
    [[nodiscard]] VaultResult read_video(const IndexNode& node, crypto::SecureBytes& out) const;

    // Remove an image from the index (its chunk is orphaned, reclaimed by Phase 7
    // compaction). NotFound if the image does not exist.
    [[nodiscard]] VaultResult remove_image(std::string_view gallery_path,
                                           std::string_view filename);

    // Remove a gallery and its whole subtree from the index. Every descendant
    // image/thumbnail chunk is orphaned (reclaimed by compaction, like remove_image).
    // Locked if not unlocked; InvalidArg for the root (""); NotFound if the path is
    // missing or names an image rather than a gallery. Persisted via the index swap.
    [[nodiscard]] VaultResult remove_gallery(std::string_view gallery_path);

    // Immediate children of `gallery_path`, ordered per that gallery's own
    // persisted sort_key (ui::gallery_sort::sort_children — folders first,
    // then Manual/insertion order or the chosen Name/Date/Size key; Phase 37).
    // Pointers are valid until the next mutating call. Empty if the path is
    // missing or not a gallery.
    [[nodiscard]] std::vector<const IndexNode*> list(std::string_view gallery_path) const;

    // The gallery's own stored sort_key (Manual if gallery_path doesn't resolve
    // to a gallery). Phase 37.
    [[nodiscard]] SortKey gallery_sort_key(std::string_view gallery_path) const;

    // Set a gallery's sort_key and persist it via the crash-safe index swap;
    // every subsequent list() of that gallery applies it. A no-op (no commit)
    // if the key is unchanged. Locked if not unlocked; NotFound if
    // gallery_path doesn't resolve to a gallery. Phase 37.
    [[nodiscard]] VaultResult set_gallery_sort(std::string_view gallery_path, SortKey key);

    // Replace a node's tag list (gallery OR image). Tags are normalised: each trimmed
    // of surrounding whitespace, empties dropped, de-duplicated case-insensitively
    // (first occurrence's casing kept). Persisted via the crash-safe index swap.
    // Locked if not unlocked; NotFound if node_path doesn't resolve.
    [[nodiscard]] VaultResult set_tags(std::string_view node_path, const std::vector<std::string>& tags);

    // Add one tag (no-op success if already present case-insensitively). InvalidArg
    // if the tag is empty/whitespace after trimming.
    [[nodiscard]] VaultResult add_tag(std::string_view node_path, std::string_view tag);

    // Remove one tag (case-insensitive; idempotent — Ok even if absent).
    [[nodiscard]] VaultResult remove_tag(std::string_view node_path, std::string_view tag);

    // Walk the decrypted in-memory tree; return every in-scope node whose name OR any
    // effective tag contains `query` (case-insensitive substring). Empty query matches
    // all in-scope nodes. Empty result if locked. Computes the cascade as it descends.
    [[nodiscard]] std::vector<SearchHit> search(std::string_view query, SearchScope scope) const;

    // The advanced-search API (tag vocabulary, weighted/grouped run_search, and
    // saved searches — Phase 18) lives on the VaultSearch facade to keep this
    // class within its method budget; it reaches in here as a friend.
    friend class VaultSearch;

    // Gallery cover montages read a descendant's thumbnail by raw span. Kept a
    // free friend (not a member) to keep Vault under the cpp:S1448 method cap.
    friend VaultResult read_thumb_span(const Vault& v, uint64_t offset,
                                       uint64_t length, crypto::SecureBytes& out);

    // UI needs the vault file size for waste display (Phase 26). Kept a free friend
    // to keep Vault under the cpp:S1448 method cap.
    friend uint64_t vault_file_bytes(const Vault& v) noexcept;

    // Flip a node's favorite flag (gallery OR image). Persisted via the crash-safe
    // index swap. Locked if not unlocked; NotFound if node_path doesn't resolve.
    [[nodiscard]] VaultResult toggle_favorite(std::string_view node_path);

    // Every favorited image (resp. gallery) across the whole tree, flat. Each hit's
    // `path` is the full slash-path; `node` is valid until the next mutating call.
    // `effective_tags` is left empty (favorites lists don't compute the tag cascade).
    // Empty while locked.
    [[nodiscard]] std::vector<SearchHit> list_favorite_images() const;
    [[nodiscard]] std::vector<SearchHit> list_favorite_galleries() const;

    // Reclaimable bytes: the part of the data region not referenced by any live
    // image/thumbnail chunk or the active index blob (orphaned chunks from
    // deletes plus superseded index blobs). 0 while locked — the index is
    // needed to know what is live.
    [[nodiscard]] uint64_t wasted_bytes() const;

    // Rewrite the vault to hold only live data: copy live chunks verbatim into
    // a new file, write a fresh index + header, fsync, then atomically rename
    // over the original. Reclaims wasted_bytes(). Invalidates all IndexNode
    // pointers previously returned by list().
    // If progress is provided, tracks total/done in chunks copied and honors cancel
    // between chunks, aborting before the atomic rename (original always intact).
    [[nodiscard]] VaultResult compact(OpProgress* progress = nullptr);

private:
    // Persist the in-memory index with the crash-safe double-buffer swap.
    [[nodiscard]] VaultResult commit_index();
    // Write the current header_ to offset 0 and fsync.
    [[nodiscard]] bool write_header();
    // Resolve a slash-separated gallery path to a node (nullptr if missing /
    // not a gallery). Empty path resolves to the root.
    [[nodiscard]] IndexNode*       find_gallery(std::string_view path);
    [[nodiscard]] const IndexNode* find_gallery(std::string_view path) const;
    // Resolve a slash-separated path to any node (gallery OR image). Intermediate
    // segments must be galleries; the final segment may be any node kind. Empty
    // path resolves to the root. Returns nullptr if any segment is missing.
    [[nodiscard]] IndexNode*       resolve_node(std::string_view path);
    [[nodiscard]] const IndexNode* resolve_node(std::string_view path) const;

    void reset() noexcept;  // close file, wipe key, clear state

    std::string                            path_;
    std::FILE*                             fp_ = nullptr;
    Header                                 header_;
    bool                                   unlocked_ = false;
    crypto::SecureBuffer<crypto::KEY_SIZE> master_key_;
    IndexNode                              root_ = IndexNode::gallery("");
    std::vector<SavedSearch>               saved_searches_;  // vault-global (Phase 18)
};

// Decrypt a thumbnail/poster chunk by its raw (offset, length) span into mlock'd
// memory. Used by gallery cover montages (Phase 19), which reference descendant
// nodes' thumbnail spans without holding the nodes. InvalidArg if length is 0;
// Locked if the vault is locked; AuthFailed on tamper/corruption.
[[nodiscard]] VaultResult read_thumb_span(const Vault& v, uint64_t offset,
                                          uint64_t length, crypto::SecureBytes& out);

// Get the vault file's current size in bytes. Returns 0 if locked or on I/O error.
// Used by the UI to display waste/compaction information (Phase 26).
[[nodiscard]] uint64_t vault_file_bytes(const Vault& v) noexcept;

} // namespace vault
