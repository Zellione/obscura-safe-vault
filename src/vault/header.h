#pragma once

// .osv fixed-size plaintext header — parse / serialise.
//
// The header is the only unencrypted region of the vault. It carries everything
// needed to derive the KEK (KDF params + salt), unwrap the master key, and locate
// the active index blob. Its layout is a fixed table of byte offsets (see the
// OFFSETS below and the spec table in CLAUDE.md / ROADMAP.md). The whole header
// occupies HEADER_SIZE bytes; the data region begins immediately after it.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "crypto/crypto.h"   // KEY_SIZE, NONCE_SIZE, TAG_SIZE, SALT_SIZE
#include "crypto/kdf.h"      // KdfParams

namespace vault {

inline constexpr std::array<char, 8> MAGIC = {'O', 'S', 'V', 'A', 'U', 'L', 'T', '\0'};
inline constexpr uint16_t FORMAT_VERSION = 1;

// Fixed header length. The data region starts here. 4 KiB gives ample room for
// the documented fields plus future expansion within the reserved padding.
inline constexpr size_t HEADER_SIZE = 4096;

// Header flags (u32 at offset 12; reserved bits are zero).
// Bit 0 (Phase 26): chunk plaintexts and the sealed index blob are framed by
// vault::chunk_codec (method byte + optional deflate). Clear = legacy raw
// vault; such vaults are read AND appended raw forever (no migration).
inline constexpr uint32_t FLAG_FRAMED_CHUNKS = 1u << 0;

// One half of the crash-safe double-buffered index pointer. `offset`/`length`
// locate the encrypted index blob (ciphertext|tag) in the data region; `nonce`
// is the XChaCha20 nonce it was sealed with. Storing the nonce here means
// flipping `active_slot` atomically commits the location *and* its nonce.
struct IndexSlot {
    uint64_t                                offset = 0;
    uint64_t                                length = 0;  // bytes of ciphertext|tag
    std::array<uint8_t, crypto::NONCE_SIZE> nonce{};
};

struct Header {
    uint16_t          version     = FORMAT_VERSION;
    uint16_t          header_size = HEADER_SIZE;
    uint32_t          flags       = 0;

    // KDF block
    uint8_t           kdf_algo = 0;  // 0 = Argon2id
    crypto::KdfParams kdf      = crypto::DEFAULT_KDF_PARAMS;
    std::array<uint8_t, crypto::SALT_SIZE> salt{};
    uint8_t           keyfile_required = 0;

    // Master-key wrap (XChaCha20-Poly1305, detached): wrapped_master_key|mk_tag
    // sealed under the KEK with mk_nonce.
    std::array<uint8_t, crypto::NONCE_SIZE> mk_nonce{};
    std::array<uint8_t, crypto::KEY_SIZE>   wrapped_master_key{};
    std::array<uint8_t, crypto::TAG_SIZE>   mk_tag{};

    // Double-buffered index pointer. slot[0] = A, slot[1] = B.
    std::array<IndexSlot, 2> slot{};
    uint8_t   active_slot = 0;  // 0 = A, 1 = B

    // Serialise into a HEADER_SIZE buffer: magic, fields at fixed offsets, then
    // zero padding to the end.
    void serialize(std::span<uint8_t, HEADER_SIZE> out) const noexcept;

    // Parse a header from the start of `raw` (which must be >= HEADER_SIZE).
    // Returns false on a short buffer, bad magic, or unsupported version.
    [[nodiscard]] static bool parse(std::span<const uint8_t> raw, Header& out) noexcept;
};

// Check if the vault uses framed chunks (Phase 26). This flag gates chunk
// and index encoding via vault::chunk_codec (method byte + optional deflate).
[[nodiscard]] constexpr bool framed_chunks(const Header& h) noexcept
{
    return (h.flags & FLAG_FRAMED_CHUNKS) != 0;
}

} // namespace vault
