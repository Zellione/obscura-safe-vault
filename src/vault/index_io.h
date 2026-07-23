#pragma once

// Index I/O and persistence: crash-safe serialization and double-buffered
// index slot swapping. Internal component of Vault. (Phase 25)

#include <cstdint>
#include <cstdio>
#include <vector>

#include "crypto/secure_mem.h"
#include "header.h"
#include "index.h"
#include "vault.h"  // VaultResult (forward-compatible with component extraction)

namespace vault {

// Internal context passed to IndexIo methods. Bundles the mutable state
// needed to persist an index atomically.
struct IndexIoContext {
    std::FILE*                             fp_;           // vault file handle (not closed by IndexIo)
    Header&                                header_;       // in-memory header, updated in-place
    const crypto::SecureBuffer<crypto::KEY_SIZE>& master_key_;  // never copied, only read
    const IndexNode&                       root_;         // index tree to serialize
    const std::vector<SavedSearch>&        saved_searches_; // vault-global saved searches
    const VaultSettings&                   settings_;       // vault-global (Phase 49)
};

// IndexIo: owns the logic for persisting the in-memory vault index with
// crash-safe double-buffer slot swapping and atomic commit.
namespace index_io {

// Serialize + seal + append the current index to the vault file, then
// atomically switch the active slot. Returns Ok on success, or IoError /
// CryptoError on failure. The crash-safe procedure is:
//   Phase A: append new sealed index blob + fsync
//   Phase B: write inactive slot header pointer + fsync
//   Phase C: flip active_slot + fsync  <- atomic commit point
// A crash before C leaves the old index valid; after C, the new one is active.
[[nodiscard]] VaultResult commit_index(IndexIoContext& ctx);

// Write the current header to offset 0 and fsync. Helper used by commit_index.
// Returns true on success, false on I/O error.
[[nodiscard]] bool write_header(std::FILE* fp, const Header& h);

} // namespace index_io

} // namespace vault
