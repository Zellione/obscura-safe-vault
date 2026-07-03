#pragma once

// Little-endian fixed-width byte I/O for the .osv on-disk format.
//
// We encode/decode explicitly (never memcpy native integers) so the format is
// byte-for-byte identical on every platform and endianness. The writers append
// to a std::vector; the ByteReader is bounds-checked so malformed input can never
// read out of range (defensive against corrupt/hostile vaults — Phase 7 fuzzing).

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace vault {

// --- Fixed-offset writers (into a caller-sized buffer) ---------------------

inline void put_u16_at(std::span<uint8_t> buf, size_t off, uint16_t v) noexcept
{
    buf[off + 0] = static_cast<uint8_t>(v);
    buf[off + 1] = static_cast<uint8_t>(v >> 8);
}

inline void put_u32_at(std::span<uint8_t> buf, size_t off, uint32_t v) noexcept
{
    buf[off + 0] = static_cast<uint8_t>(v);
    buf[off + 1] = static_cast<uint8_t>(v >> 8);
    buf[off + 2] = static_cast<uint8_t>(v >> 16);
    buf[off + 3] = static_cast<uint8_t>(v >> 24);
}

inline void put_u64_at(std::span<uint8_t> buf, size_t off, uint64_t v) noexcept
{
    for (int i = 0; i < 8; ++i) buf[off + i] = static_cast<uint8_t>(v >> (8 * i));
}

inline void put_bytes_at(std::span<uint8_t> buf, size_t off,
                         std::span<const uint8_t> src) noexcept
{
    if (!src.empty()) std::memcpy(buf.data() + off, src.data(), src.size());
}

// --- Fixed-offset readers (from a buffer known to be large enough) ---------

inline uint16_t get_u16_at(std::span<const uint8_t> buf, size_t off) noexcept
{
    return static_cast<uint16_t>(
        static_cast<unsigned>(buf[off + 0]) |
        (static_cast<unsigned>(buf[off + 1]) << 8));
}

inline uint32_t get_u32_at(std::span<const uint8_t> buf, size_t off) noexcept
{
    return static_cast<uint32_t>(buf[off + 0]) |
           static_cast<uint32_t>(buf[off + 1]) << 8 |
           static_cast<uint32_t>(buf[off + 2]) << 16 |
           static_cast<uint32_t>(buf[off + 3]) << 24;
}

inline uint64_t get_u64_at(std::span<const uint8_t> buf, size_t off) noexcept
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(buf[off + i]) << (8 * i);
    return v;
}

// --- Appending writer (for variable-length, recursive index serialisation) -

class ByteWriter {
public:
    explicit ByteWriter(std::vector<uint8_t>& out) noexcept : out_(out) {}

    void u8(uint8_t v)   { out_.push_back(v); }
    void u16(uint16_t v) { for (int i = 0; i < 2; ++i) out_.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) out_.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    void u64(uint64_t v) { for (int i = 0; i < 8; ++i) out_.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    void bytes(std::span<const uint8_t> b) { out_.insert(out_.end(), b.begin(), b.end()); }

private:
    std::vector<uint8_t>& out_;
};

// --- Bounds-checked reader (for parsing untrusted serialised blobs) ---------
//
// Every read advances a cursor and verifies it stays within the buffer. On the
// first short read the reader latches an error; subsequent reads return zero and
// keep the error set, so callers can check ok() once at the end.

class ByteReader {
public:
    explicit ByteReader(std::span<const uint8_t> in) noexcept : in_(in) {}

    [[nodiscard]] bool   ok()        const noexcept { return ok_; }
    [[nodiscard]] size_t remaining() const noexcept { return ok_ ? in_.size() - pos_ : 0; }

    uint8_t  u8()  { return need(1) ? in_[pos_++] : 0; }
    uint16_t u16() { return get<uint16_t>(2); }
    uint32_t u32() { return get<uint32_t>(4); }
    uint64_t u64() { return get<uint64_t>(8); }

    // Copy `n` bytes into `dst` (which must be exactly n). Fails if short.
    void bytes(std::span<uint8_t> dst)
    {
        if (!need(dst.size())) return;
        std::memcpy(dst.data(), in_.data() + pos_, dst.size());
        pos_ += dst.size();
    }

private:
    [[nodiscard]] bool need(size_t n) noexcept
    {
        if (!ok_) return false;
        if (in_.size() - pos_ < n) { ok_ = false; return false; }
        return true;
    }

    template <typename T>
    T get(size_t n)
    {
        if (!need(n)) return 0;
        T v = 0;
        for (size_t i = 0; i < n; ++i) {
            const T byte_val = static_cast<T>(in_[pos_ + i]);
            v |= static_cast<T>(byte_val << static_cast<int>(8 * i));
        }
        pos_ += n;
        return v;
    }

    std::span<const uint8_t> in_;
    size_t                   pos_ = 0;
    bool                     ok_  = true;
};

} // namespace vault
