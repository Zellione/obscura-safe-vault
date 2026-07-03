#include "vault/chunk_codec.h"

#include <cstring>
#include <limits>

#include "miniz.h"

namespace vault::chunk_codec {
namespace {

void put_u64le(uint8_t* p, uint64_t v) noexcept
{
    for (int i = 0; i < 8; ++i) p[static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
}

uint64_t get_u64le(const uint8_t* p) noexcept
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[static_cast<size_t>(i)]) << (8 * i);
    return v;
}

// mz_ulong is `unsigned long` — 32-bit on Windows. Payloads beyond its range
// (or beyond deflate's own 4 GiB-ish limits) simply store raw.
constexpr uint64_t MZ_LEN_MAX = std::numeric_limits<mz_ulong>::max();

// Uniform resize/data over vector (never fails) and SecureBytes (may fail).
// Declared BEFORE the templates/functions that call them (C++ two-phase lookup).
bool resize_buf(std::vector<uint8_t>& b, size_t n) noexcept
{
    b.resize(n);
    return true;
}

bool resize_buf(crypto::SecureBytes& b, size_t n) noexcept { return b.resize(n); }

uint8_t* buf_data(std::vector<uint8_t>& b) noexcept { return b.data(); }

uint8_t* buf_data(crypto::SecureBytes& b) noexcept { return b.data(); }

// Compress into `dst` (pre-sized to mz_compressBound). Returns compressed
// length, or 0 when compression failed or did not win the >=5% threshold.
size_t try_deflate(std::span<const uint8_t> payload, std::span<uint8_t> dst) noexcept
{
    if (payload.empty() || payload.size() > MZ_LEN_MAX) return 0;
    auto comp_len = static_cast<mz_ulong>(dst.size());
    if (mz_compress2(dst.data(), &comp_len, payload.data(),
                     static_cast<mz_ulong>(payload.size()), MZ_DEFAULT_LEVEL) != MZ_OK) {
        return 0;
    }
    // Store-if-smaller: framed deflate must be <= 95% of framed raw.
    const uint64_t framed_deflate = DEFLATE_HDR + static_cast<uint64_t>(comp_len);
    const uint64_t framed_raw     = RAW_HDR + payload.size();
    return framed_deflate * 100 <= framed_raw * 95 ? static_cast<size_t>(comp_len) : 0;
}

// Shared encode over any byte sink with resize(n)+data(). `Buf` is
// std::vector<uint8_t> or crypto::SecureBytes (chunk payloads are decrypted
// content, so their frames stay in mlock'd memory; the index blob keeps the
// existing plain-vector metadata posture).
template <typename Buf>
bool encode_impl(std::span<const uint8_t> payload, Buf& out) noexcept
{
    // Scratch for the compressed attempt. SecureBytes for both cases would be
    // wasteful for the index; match the output buffer's locking.
    Buf scratch;
    const uint64_t bound =
        payload.size() > MZ_LEN_MAX
            ? 0
            : static_cast<uint64_t>(mz_compressBound(static_cast<mz_ulong>(payload.size())));
    size_t comp_len = 0;
    if (bound != 0) {
        if (!resize_buf(scratch, static_cast<size_t>(bound))) return false;
        comp_len = try_deflate(payload, {buf_data(scratch), static_cast<size_t>(bound)});
    }

    if (comp_len != 0) {
        if (!resize_buf(out, DEFLATE_HDR + comp_len)) return false;
        buf_data(out)[0] = METHOD_DEFLATE;
        put_u64le(buf_data(out) + 1, payload.size());
        std::memcpy(buf_data(out) + DEFLATE_HDR, buf_data(scratch), comp_len);
    } else {
        if (!resize_buf(out, RAW_HDR + payload.size())) return false;
        buf_data(out)[0] = METHOD_RAW;
        if (!payload.empty()) std::memcpy(buf_data(out) + RAW_HDR, payload.data(), payload.size());
    }
    return true;
}

template <typename Buf>
bool decode_impl(std::span<const uint8_t> framed, Buf& out) noexcept
{
    if (framed.empty()) return false;

    if (framed[0] == METHOD_RAW) {
        const auto payload = framed.subspan(RAW_HDR);
        if (!resize_buf(out, payload.size())) return false;
        if (!payload.empty()) std::memcpy(buf_data(out), payload.data(), payload.size());
        return true;
    }

    if (framed[0] != METHOD_DEFLATE || framed.size() < DEFLATE_HDR + 1) return false;

    const uint64_t orig_len = get_u64le(framed.data() + 1);
    const uint64_t comp_len = framed.size() - DEFLATE_HDR;
    // Bomb guard: bound the claimed output BEFORE allocating it.
    if (orig_len > comp_len * MAX_INFLATE_RATIO + INFLATE_SLACK) return false;
    if (orig_len > MZ_LEN_MAX || orig_len == 0) return false;

    if (!resize_buf(out, static_cast<size_t>(orig_len))) return false;
    auto dst_len = static_cast<mz_ulong>(orig_len);
    if (auto src_len = static_cast<mz_ulong>(comp_len);
        mz_uncompress2(buf_data(out), &dst_len, framed.data() + DEFLATE_HDR, &src_len) != MZ_OK ||
        dst_len != static_cast<mz_ulong>(orig_len) || src_len != static_cast<mz_ulong>(comp_len)) {
        (void)resize_buf(out, 0);  // SecureBytes: wipes the partial plaintext
        return false;
    }
    return true;
}

}  // namespace

bool encode_frame(std::span<const uint8_t> payload, crypto::SecureBytes& out) noexcept
{ return encode_impl(payload, out); }

bool encode_frame(std::span<const uint8_t> payload, std::vector<uint8_t>& out) noexcept
{ return encode_impl(payload, out); }

bool decode_frame(std::span<const uint8_t> framed, crypto::SecureBytes& out) noexcept
{ return decode_impl(framed, out); }

bool decode_frame(std::span<const uint8_t> framed, std::vector<uint8_t>& out) noexcept
{ return decode_impl(framed, out); }

}  // namespace vault::chunk_codec
