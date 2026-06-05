#pragma once

// Phase 2 stub: .osv vault container — header, index tree, chunk store.
// Full implementation in Phase 2.

#include <cstdint>
#include <string>

namespace vault {

// Magic bytes at the start of every .osv file
inline constexpr char MAGIC[8] = {'O','S','V','A','U','L','T','\0'};
inline constexpr uint16_t FORMAT_VERSION = 1;

// TODO (Phase 2): Header    — KDF params, master-key wrap, index slots
// TODO (Phase 2): IndexNode — gallery tree (free-nesting; images only at leaves)
// TODO (Phase 2): ChunkStore — append-only encrypted blobs with random nonces
// TODO (Phase 2): Vault      — open/create, unlock(password, keyfile?),
//                              add_image, remove_image, read_image_to_memory,
//                              compaction, crash-safe index swap

} // namespace vault
