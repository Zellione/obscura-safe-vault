#pragma once

// Chunk plaintext framing (Phase 26). Frames a payload as
//   method u8 (0=raw, 1=deflate) | [orig_len u64 LE | zlib stream]
// The frame lives INSIDE the AEAD plaintext, so the method byte is
// authenticated. Deflate is kept only when the framed compressed form is
// <= 95% of the framed raw form (store-if-smaller) — already-compressed
// media always falls back to raw with 1 byte of overhead.

#include <cstdint>
#include <span>
#include <vector>

#include "crypto/secure_mem.h"

namespace vault::chunk_codec {

inline constexpr uint8_t METHOD_RAW     = 0;
inline constexpr uint8_t METHOD_DEFLATE = 1;

// Frame sizes: raw = 1 method byte; deflate = method + orig_len u64.
inline constexpr size_t RAW_HDR     = 1;
inline constexpr size_t DEFLATE_HDR = 1 + 8;

// deflate's hard worst-case expansion bound (~1032:1). A claimed orig_len
// beyond compressed_size * MAX_INFLATE_RATIO (+ slack for tiny inputs) is
// hostile and rejected BEFORE any allocation.
inline constexpr uint64_t MAX_INFLATE_RATIO = 1032;
inline constexpr uint64_t INFLATE_SLACK     = 64;

[[nodiscard]] bool encode_frame(std::span<const uint8_t> payload, crypto::SecureBytes& out) noexcept;
[[nodiscard]] bool encode_frame(std::span<const uint8_t> payload, std::vector<uint8_t>& out) noexcept;
[[nodiscard]] bool decode_frame(std::span<const uint8_t> framed, crypto::SecureBytes& out) noexcept;
[[nodiscard]] bool decode_frame(std::span<const uint8_t> framed, std::vector<uint8_t>& out) noexcept;

// --- fault injection (allocation-failure tests) -----------------------------
// std::vector<uint8_t>::resize() can throw (bad_alloc / length_error) on a
// real allocation failure. There is no portable way to make a real allocation
// fail on demand, so tests arm this counter to make the Nth upcoming
// vector-buffer resize inside encode_frame/decode_frame throw (0 = the very
// next resize). Disarmed by default and after firing. Mirrors
// vault::fileutil's sync_fail_after/rename_fail_after pattern.
inline int& resize_fail_after() noexcept
{
    static int n = -1;
    return n;
}

inline void inject_resize_failure(int after_calls) noexcept { resize_fail_after() = after_calls; }
inline void clear_resize_failure() noexcept                 { resize_fail_after() = -1; }

}  // namespace vault::chunk_codec
