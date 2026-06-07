#pragma once

// Append-only encrypted blob region of a .osv file.
//
// ChunkStore is the single owner of data-region file I/O. Data/thumbnail chunks
// are encrypted with the master key and a fresh random nonce per chunk (the
// packed `nonce|ciphertext|tag` form). The encrypted index blob is written via
// the raw API (it is sealed detached by the vault, with its nonce in the header).
//
// The store borrows an already-open FILE* and the master key; it owns neither.
// The vault is responsible for opening/closing the file and for the fsync
// ordering that makes writes crash-safe.

#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

#include "crypto/crypto.h"
#include "crypto/secure_mem.h"

namespace vault {

// Location of a chunk in the data region: byte `offset` from the start of the
// file, `length` on-disk bytes (for chunks that's nonce|ciphertext|tag).
struct ChunkSpan {
    uint64_t offset = 0;
    uint64_t length = 0;
};

class ChunkStore {
public:
    // `fp` must be opened read+write binary and outlive the store. `key` is
    // borrowed (must outlive the store) — typically the vault's master key.
    ChunkStore(std::FILE* fp, std::span<const uint8_t, crypto::KEY_SIZE> key) noexcept
        : fp_(fp), key_(key) {}

    // Encrypt `plaintext` (random nonce) and append at end of file. On success
    // fills `out` with the written location. Returns false on RNG or I/O failure.
    [[nodiscard]] bool append_chunk(std::span<const uint8_t> plaintext, ChunkSpan& out) noexcept;

    // Read + decrypt the chunk at `span`, verifying the tag. Plaintext into `out`.
    [[nodiscard]] bool read_chunk(ChunkSpan span, std::vector<uint8_t>& out) const noexcept;

    // Same, but decrypts straight into mlock'd memory (for decrypted image data).
    [[nodiscard]] bool read_chunk(ChunkSpan span, crypto::SecureBytes& out) const noexcept;

    // Append already-prepared bytes verbatim (the sealed index blob). Returns the
    // offset they were written at.
    [[nodiscard]] bool append_raw(std::span<const uint8_t> bytes, uint64_t& out_offset) noexcept;

    // Read `length` raw bytes at `offset` (no decryption) into `out`.
    [[nodiscard]] bool read_raw(uint64_t offset, uint64_t length, std::vector<uint8_t>& out) const noexcept;

    // Flush buffered writes and fsync to durable storage. Call between the
    // ordered steps of a crash-safe write.
    [[nodiscard]] bool sync() noexcept;

private:
    // Read `length` raw bytes at `offset` into a span sized exactly `length`.
    [[nodiscard]] bool read_at(uint64_t offset, std::span<uint8_t> dst) const noexcept;
    // Append `bytes` at EOF, returning the start offset.
    [[nodiscard]] bool append_at_end(std::span<const uint8_t> bytes, uint64_t& out_offset) noexcept;

    std::FILE*                                fp_;
    std::span<const uint8_t, crypto::KEY_SIZE> key_;
};

} // namespace vault
