#pragma once

// Thread-safe staging API for async image/video import (Phase 50).
//
// Split add_image/add_video into:
// - stage_image/stage_video: encrypt & append chunks under the write mutex
//   (thread-safe, tree-untouched, no fsync)
// - attach_staged: insert the staged node into the tree (main thread only, no commit)
// - ensure_gallery_path: create gallery hierarchy without final commit
//
// Existing add_image/add_video recompose from these, preserving identical behavior.

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "index.h"

namespace vault {

// Forward declarations.
class Vault;

// Pool-computed decode result for an image (Task 7 fills this from the decode
// pool; nullptr => stage_image decodes inline, exactly like add_image today).
struct StagedThumb {
    std::vector<uint8_t> thumb_jpeg;                    // empty => no thumbnail
    ImageFormat          format   = ImageFormat::Unknown;
    uint32_t             width    = 0;
    uint32_t             height   = 0;
    bool                 animated = false;
};

struct StagedNode {
    VaultResult status = VaultResult::Ok;
    IndexNode   node{};   // fully populated, UNATTACHED (chunks already on disk)
};

// THREAD-SAFE (any thread): encrypt + append the data/thumb chunks under the
// vault's write mutex and return a ready-to-attach node. Never touches the
// index tree; never fsyncs (the commit lane's Phase A fsync covers these
// appends — see index_io). Locked if the vault is locked; InvalidArg for an
// unsafe filename.
[[nodiscard]] StagedNode stage_image(Vault& v, std::span<const uint8_t> file_data,
                                     std::string_view filename,
                                     const StagedThumb* precomputed = nullptr);

// THREAD-SAFE (any thread): encrypt + append video chunks under the vault's
// write mutex. Never touches the index tree; never fsyncs. Locked if the vault
// is locked; InvalidArg for an unsafe filename.
[[nodiscard]] StagedNode stage_video(Vault& v, std::span<const uint8_t> file_data,
                                     std::string_view filename,
                                     uint32_t chunk_size = VIDEO_CHUNK_SIZE);

// MAIN THREAD ONLY: attach a staged node under gallery_path. AlreadyExists if
// the name is taken (the staged chunks stay orphaned — reclaimed by compact,
// same class as a cancelled import). NO commit_index — callers batch.
[[nodiscard]] VaultResult attach_staged(Vault& v, std::string_view gallery_path,
                                        IndexNode&& node);

// MAIN THREAD ONLY: create_gallery without the trailing commit_index (used by
// the archive-import drain; Ok if it already exists as a gallery).
[[nodiscard]] VaultResult ensure_gallery_path(Vault& v, std::string_view gallery_path);

}  // namespace vault
