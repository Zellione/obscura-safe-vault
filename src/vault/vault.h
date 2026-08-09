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
#include <functional>
#include <memory>
#include <mutex>
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

class CommitLane;

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

// Video chunk size (1 MiB plaintext split).
inline constexpr uint32_t VIDEO_CHUNK_SIZE = 1u << 20;

// Forward declare staging structures (full definitions in staging.h, which is included at EOF).
// These are needed for friend declarations inside the Vault class.
struct StagedThumb;
struct StagedVideoInfo;
struct StagedNode;

enum class SearchScope { Images, Galleries, Both };

// Result stats of Vault::prune_tags: how many tags were removed, from how
// many distinct nodes.
struct PruneTagsStats {
    std::size_t tags_removed  = 0;
    std::size_t nodes_touched = 0;
};

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

// Result stats of batch media removal (duplicate finder): how many media nodes were
// removed and how many named paths were missing or non-media.
struct RemoveBatchStats {
    std::size_t removed = 0;
    std::size_t missing = 0;
};

// Probed video metadata handed back from a migration worker. Mirrors
// media::VideoProbeResult field-for-field ON PURPOSE: vault.h must not include
// media/video_probe.h (vault/ does not depend on media/), and the poster is
// borrowed as a span rather than owned.
struct VideoProbeApply {
    VideoCodec               codec       = VideoCodec::Unknown;
    uint32_t                 width       = 0;
    uint32_t                 height      = 0;
    uint64_t                 duration_us = 0;
    std::span<const uint8_t> poster_jpeg;   // empty = leave the poster alone
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

    // Phase 50: CommitLane thread accesses fp_, header_, master_key_, root_,
    // saved_searches_, settings_, and header_mutex_ for asynchronous index
    // commits via the worker loop.
    friend class CommitLane;

    // Phase 50: thread-safe staging API (staging.h). Kept as friends to keep
    // Vault under the cpp:S1448 method cap. StagedNode/StagedThumb are forward
    // declared before the class definition.
    friend StagedNode stage_image(Vault& v, std::span<const uint8_t> file_data,
                                  std::string_view filename,
                                  const StagedThumb* precomputed);
    friend StagedNode stage_video(Vault& v, std::span<const uint8_t> file_data,
                                  std::string_view filename,
                                  uint32_t chunk_size,
                                  const StagedVideoInfo* precomputed);
    friend VaultResult attach_staged(Vault& v, std::string_view gallery_path,
                                     IndexNode&& node);
    friend VaultResult ensure_gallery_path(Vault& v, std::string_view gallery_path);
    friend VaultResult add_image_prestaged(Vault& v, std::string_view,
                                           std::span<const uint8_t>, std::string_view,
                                           const StagedThumb&, uint64_t);
    friend VaultResult add_video_prestaged(Vault& v, std::string_view,
                                           std::span<const uint8_t>, std::string_view,
                                           const StagedVideoInfo&, uint64_t);
    friend VaultResult attach_image_prestaged(Vault& v, std::string_view,
                                              std::span<const uint8_t>, std::string_view,
                                              const StagedThumb&, uint64_t);
    friend VaultResult attach_video_prestaged(Vault& v, std::string_view,
                                              std::span<const uint8_t>, std::string_view,
                                              const StagedVideoInfo&, uint64_t);
    friend VaultResult commit_staged(Vault& v);

    [[nodiscard]] bool is_unlocked() const noexcept { return unlocked_; }

    // Create a gallery at `gallery_path` (slash-separated), creating intermediate
    // galleries as needed. Fails with InvalidArg if any path segment is an image.
    // A gallery already holding media may gain a sub-gallery (Phase 46).
    // AlreadyExists if the gallery already exists.
    [[nodiscard]] VaultResult create_gallery(std::string_view gallery_path);

    // Encrypt and store `file_data` as an image named `filename` in the gallery
    // `gallery_path` (root is ""). The target may also hold sub-galleries
    // (Phase 46). AlreadyExists if the name is taken.
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

    // Test-only seam (defined in tests/vault/test_video.cpp, not part of any
    // production translation unit): resets a just-imported, fully-decodable
    // video node's metadata back to the Unknown/0/empty state that would need
    // fixing, simulating "imported before this build could decode its codec".
    // Production code can never produce that state for a file the current build
    // CAN decode — only a real FFmpeg capability change between import time and
    // now can.
    friend void test_only_force_video_codec_unknown(Vault& v, std::string_view node_path);

    // Phase 65 test seam: force an animatable image to appear un-backfilled by
    // clearing the animated flag, simulating "imported before animation detection
    // was implemented". Used to test scan_migration's image-counting arm.
    friend void test_only_force_image_animated_unknown(Vault& v, std::string_view node_path);

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

    // gallery_sort_key/set_gallery_sort (Phase 37) are free friends, not members,
    // to keep Vault under the cpp:S1448 method cap.
    friend SortKey gallery_sort_key(const Vault& v, std::string_view gallery_path);
    friend VaultResult set_gallery_sort(Vault& v, std::string_view gallery_path, SortKey key);

    // vault_settings/set_vault_settings (Phase 49) are free friends, not members,
    // for the same cpp:S1448 method-cap reason as gallery_sort_key/set_gallery_sort.
    friend const VaultSettings& vault_settings(const Vault& v) noexcept;
    friend VaultResult set_vault_settings(Vault& v, VaultSettings s);

    // rename_node (Phase 44) is a free friend for the same cpp:S1448 reason as
    // gallery_sort_key/set_gallery_sort above.
    friend VaultResult rename_node(Vault& v, std::string_view gallery_path,
                                   std::string_view old_name, std::string_view new_name);

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

    // Remove every tag on every node (root included) for which `keep` returns
    // false, in one index commit — no commit when nothing matched. The predicate
    // is pure policy (e.g. ui::tag_has_renderable_text for the junk-tag cleanup).
    // `stats` (if non-null) is always written — zeroed on Locked/InvalidArg
    // (InvalidArg for an empty predicate).
    [[nodiscard]] VaultResult prune_tags(const std::function<bool(std::string_view)>& keep,
                                         PruneTagsStats* stats);

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

    // Batch media removal (duplicate finder): erase every media node named by
    // `node_paths`, then ONE commit_index() + one auto_reclaim_space(). Kept a
    // free friend to keep Vault under the cpp:S1448 method cap.
    friend VaultResult remove_media_batch(Vault& v,
                                          std::span<const std::string> node_paths,
                                          RemoveBatchStats* stats);

    // Batch favorite set (Phase 68 multiselect): flip every resolving path to
    // `value`, ONE commit_index(). Kept a free friend for the same S1448 reason.
    friend VaultResult set_favorites_batch(Vault& v,
                                           std::span<const std::string> node_paths,
                                           bool value);

    // Phase 65 migration: apply probed metadata WITHOUT committing, so a whole
    // migration pass costs one index write instead of one per node.
    friend VaultResult apply_video_probe(Vault& v, std::string_view node_path,
                                         const VideoProbeApply& probe);
    friend VaultResult apply_image_animated(Vault& v, std::string_view node_path,
                                            bool animated);
    friend VaultResult commit_migration(Vault& v, VaultSettings settings);

    // Phase 50: while the import queue is active, App points this at the
    // CommitLane; Vault::commit_index() then routes through the lane
    // (serialize + enqueue, async durability) instead of committing
    // synchronously. Null (the default) = synchronous commit, exactly the
    // pre-Phase-50 behavior. Main-thread only.
    void set_commit_router(CommitLane* lane) noexcept { commit_router_ = lane; }

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

    // Compact the vault IN PLACE (Phase 60): pack live chunks down into dead
    // space (deleted chunks + superseded index blobs), commit the index in
    // batches via the crash-safe slot swap, write the final index blob just
    // after the packed data, then truncate the dead tail (+ hole-punch any
    // residual gaps where supported). Byte-verbatim ciphertext moves — never
    // a decrypt. O(1) extra disk: no temp copy, no rename, handles stay open.
    // Crash-safe by construction: a move's destination is always dead per the
    // last-committed index, so a crash at any instant reopens to a valid,
    // partially-compacted vault; re-running continues. Cancel (via progress)
    // keeps completed work and returns Ok; progress counts MiB moved.
    // Invalidates all IndexNode pointers previously returned by list().
    [[nodiscard]] VaultResult compact(OpProgress* progress = nullptr);

    // Reclaim orphaned chunk space IN PLACE by punching holes in the dead spans
    // (deleted chunks + the superseded index slot), instead of compact()'s copy-
    // to-a-new-file rewrite. Offsets are unchanged and the index is untouched, so
    // there is no transient ~2x disk-usage spike and no reopen. The freed blocks
    // return to the filesystem; the file's logical length is unchanged (it stays
    // sparse), so wasted_bytes() — a LOGICAL measure — still reports the holes.
    // Crash-safe by construction: only dead bytes are touched, never live data or
    // the header. Linux only; a no-op returning Ok where hole-punch is
    // unsupported. Locked if the vault is locked.
    [[nodiscard]] VaultResult reclaim();

    // Resolve a slash-separated path to any node (gallery OR image). Intermediate
    // segments must be galleries; the final segment may be any node kind. Empty
    // path resolves to the root. Returns nullptr if any segment is missing.
    // Phase 58: const overload used by ImageViewer::on_vault_changed to re-resolve
    // collection album paths after a vault mutation.
    [[nodiscard]] IndexNode*       resolve_node(std::string_view path);
    [[nodiscard]] const IndexNode* resolve_node(std::string_view path) const;

private:
    // Best-effort space reclamation after a delete, gated on AUTO_COMPACT_*:
    // in-place hole punching where supported (no disk spike), else a full
    // compact(). Shared by remove_image / remove_gallery.
    void auto_reclaim_space();

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
    // Phase 50: dedicated read-only handle. All const read paths (read_image /
    // read_thumbnail / read_video / read_thumb_span / VideoSource streaming) use
    // this so a background stage/commit append on fp_ can never share a file
    // position with a main-thread read. Opened by create()/open(), closed by
    // reset(). Chunks are immutable once appended, so reads need no lock.
    std::FILE*                             read_fp_ = nullptr;
    // Phase 58: dedicated handle for BACKGROUND thumbnail reads (DecodeWorker
    // fetch stage). read_fp_ stays main-thread-only (read_image / read_video /
    // VideoSource / FileOpJob), so thumb I/O can never stall video streaming or
    // race a file-op worker. thumb_mutex_ serialises the seek+read of one
    // thumbnail AND guards close-on-lock: reset() flips unlocked_ and closes
    // thumb_fp_ under this mutex BEFORE wiping the master key, so an in-flight
    // fetch either completes against valid state or observes Locked. Never
    // taken together with write_mutex_/header_mutex_ (no nesting, no ordering).
    std::FILE*                             thumb_fp_ = nullptr;
    std::unique_ptr<std::mutex>            thumb_mutex_;
    // Phase 50: serialises ALL writes to fp_ (chunk appends from stage_*,
    // index slot writes from the commit lane / commit_index). unique_ptr keeps
    // Vault movable. Held for one whole chunk per acquisition — never released
    // mid-chunk (an index-blob append at EOF would interleave into the
    // half-written chunk).
    std::unique_ptr<std::mutex>            write_mutex_;
    // Phase 50: guards index-slot fields (header_.slot[*] and header_.active_slot)
    // which can race between the main thread reading/writing and the commit lane
    // thread reading in commit_plain_blob. Immutable-after-unlock fields (salt, kdf,
    // wrapped key, flags) need no guard and are never modified after create/unlock.
    std::unique_ptr<std::mutex>            header_mutex_;
    Header                                 header_;
    bool                                   unlocked_ = false;
    crypto::SecureBuffer<crypto::KEY_SIZE> master_key_;
    IndexNode                              root_ = IndexNode::gallery("");
    std::vector<SavedSearch>               saved_searches_;  // vault-global (Phase 18)
    VaultSettings                          settings_;        // vault-global (Phase 49)
    // Phase 50: commit router hook. While the import queue is active, this points
    // to a CommitLane for asynchronous index commits. Null = synchronous commits.
    // Main-thread only.
    CommitLane*                            commit_router_ = nullptr;
};

// Decrypt a thumbnail/poster chunk by its raw (offset, length) span into mlock'd
// memory. Used by gallery cover montages (Phase 19), which reference descendant
// nodes' thumbnail spans without holding the nodes. InvalidArg if length is 0;
// Locked if the vault is locked; AuthFailed on tamper/corruption.
[[nodiscard]] VaultResult read_thumb_span(const Vault& v, uint64_t offset,
                                          uint64_t length, crypto::SecureBytes& out);

// Batch media removal (duplicate finder): erase every media node named by
// `node_paths` (full slash-paths). Missing / non-media paths are counted, not
// errors. ONE commit_index() for the whole batch (none if nothing was removed),
// then one auto_reclaim_space(). Locked if locked; IoError if the commit fails
// (tree already mutated — same contract as remove_image's failed commit).
[[nodiscard]] VaultResult remove_media_batch(Vault& v,
                                             std::span<const std::string> node_paths,
                                             RemoveBatchStats* stats = nullptr);

// Batch favorite set (Phase 68): set every resolving path's favorite flag to
// `value` (galleries and media alike); non-resolving paths are skipped, not
// errors. ONE commit_index() for the whole batch — and none at all when no
// node actually changed. Locked if locked; IoError if the commit fails.
[[nodiscard]] VaultResult set_favorites_batch(Vault& v,
                                              std::span<const std::string> node_paths,
                                              bool value);

// Get the vault file's current size in bytes. Returns 0 if locked or on I/O error.
// Used by the UI to display waste/compaction information (Phase 26).
[[nodiscard]] uint64_t vault_file_bytes(const Vault& v) noexcept;

// The gallery's own stored sort_key (Manual if gallery_path doesn't resolve to a
// gallery). Phase 37.
[[nodiscard]] SortKey gallery_sort_key(const Vault& v, std::string_view gallery_path);

// Set a gallery's sort_key and persist it via the crash-safe index swap; every
// subsequent list() of that gallery applies it. A no-op (no commit) if the key
// is unchanged. Locked if not unlocked; NotFound if gallery_path doesn't
// resolve to a gallery. Phase 37.
[[nodiscard]] VaultResult set_gallery_sort(Vault& v, std::string_view gallery_path, SortKey key);

// The vault's global settings — tag categories, default sort, tile-tag flag
// (Phase 49). Returns the seeded set for a vault that has never stored any.
// Safe to call while locked (returns the last-loaded value).
[[nodiscard]] const VaultSettings& vault_settings(const Vault& v) noexcept;

// Replace the vault's global settings and persist them via the crash-safe index
// swap. Locked if not unlocked. Phase 49.
[[nodiscard]] VaultResult set_vault_settings(Vault& v, VaultSettings s);

// Write probed metadata onto a video node and append its poster chunk if the
// node has none, WITHOUT committing the index. Locked if locked; NotFound if
// the path does not resolve to a video; IoError if the poster append fails.
// A node that already has a real codec is left alone (Ok, no write).
// Coordinator-thread only — mutates the tree and appends to fp_.
[[nodiscard]] VaultResult apply_video_probe(Vault& v, std::string_view node_path,
                                            const VideoProbeApply& probe);

// Set an image node's animated flag WITHOUT committing the index. Ok (no write)
// when the flag is already correct or the format cannot animate; NotFound if the
// path does not resolve to an image. Coordinator-thread only.
[[nodiscard]] VaultResult apply_image_animated(Vault& v, std::string_view node_path,
                                               bool animated);

// Persist `settings` (carrying the Phase 65 watermark) together with every
// pending apply_* mutation in ONE commit_index(). Locked if locked; IoError if
// the commit fails (the tree is already mutated — the remove_media_batch
// contract).
[[nodiscard]] VaultResult commit_migration(Vault& v, VaultSettings settings);

// Rename an image, video, or gallery's own `name` field in place — a pure
// leaf-field edit. Descendants, tags, favorite flag, sort key, and cover all
// live on the node itself (paths are computed on the fly, never persisted),
// so nothing else needs updating. Locked if not unlocked; InvalidArg if
// `new_name` fails is_safe_node_name; AlreadyExists if a sibling already
// holds `new_name`; NotFound if `old_name` doesn't resolve under
// `gallery_path`. A no-op success if new_name == old_name. Phase 44.
[[nodiscard]] VaultResult rename_node(Vault& v, std::string_view gallery_path,
                                      std::string_view old_name, std::string_view new_name);

}  // namespace vault
