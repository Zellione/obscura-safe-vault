#pragma once

// Phase 99 (OSV-AUD-004): a stored chunk's decrypt context.
//
// A ChunkRef is the offset/length span PLUS the logical identity the AEAD
// binds (owner node_id, per-record id, domain, sequence, and the
// legacy/migrated flag). Everything derives from the authenticated index, so
// the reader builds byte-identical associated data from the value alone —
// span-snapshot structures (gallery covers, the duplicate scan, migration
// workers) copy the value so their any-thread reads never need the (possibly
// dangling) IndexNode.
//
// SDL-free, Vault-free, I/O-free: a pure value type usable from the headless
// UI models (src/ui/dup_model.h) without dragging in the Vault.

#include <array>
#include <cstdint>

#include "crypto/crypto.h"   // ChunkDomain, NODE_ID_SIZE

namespace vault {

struct ChunkRef {
    uint64_t offset = 0;
    uint64_t length = 0;
    crypto::ChunkDomain domain     = crypto::ChunkDomain::Thumb;
    std::array<uint8_t, crypto::NODE_ID_SIZE> node_id{};
    std::array<uint8_t, crypto::NODE_ID_SIZE> record{};
    uint32_t sequence      = 0;
    bool     context_bound = false;

    friend bool operator==(const ChunkRef&, const ChunkRef&) noexcept = default;
};

}  // namespace vault