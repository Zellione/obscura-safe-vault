#include "chunk_store.h"

#include <print>

#include "chunk_codec.h"
#include "crypto/aead.h"
#include "file_util.h"

namespace vault {

using fileutil::file_size;
using fileutil::seek_end;
using fileutil::seek_to;

bool ChunkStore::append_at_end(std::span<const uint8_t> bytes, uint64_t& out_offset) noexcept
{
    uint64_t end = 0;
    if (!seek_end(fp_, end)) {
        std::println(stderr, "[vault::chunk_store] seek to end failed");
        return false;
    }
    if (!bytes.empty() &&
        std::fwrite(bytes.data(), 1, bytes.size(), fp_) != bytes.size()) {
        std::println(stderr, "[vault::chunk_store] write of {} bytes failed", bytes.size());
        return false;
    }
    out_offset = end;
    return true;
}

bool ChunkStore::span_in_file(uint64_t offset, uint64_t length) const noexcept
{
    uint64_t size = 0;
    return file_size(fp_, size) && offset <= size && length <= size - offset;
}

bool ChunkStore::read_at(uint64_t offset, std::span<uint8_t> dst) const noexcept
{
    // Bounds-check before reading so a corrupt span can't read past EOF.
    if (!span_in_file(offset, dst.size())) return false;
    if (!seek_to(fp_, offset)) return false;
    if (dst.empty()) return true;
    return std::fread(dst.data(), 1, dst.size(), fp_) == dst.size();
}

bool ChunkStore::append_chunk(std::span<const uint8_t> plaintext, ChunkSpan& out) noexcept
{
    std::vector<uint8_t> chunk;
    if (framed_) {
        // The frame holds (possibly compressed) decrypted content: mlock'd.
        crypto::SecureBytes framed;
        if (!chunk_codec::encode_frame(plaintext, framed)) return false;
        if (!crypto::encrypt_chunk(key_, framed.as_span(), chunk)) return false;
    } else {
        if (!crypto::encrypt_chunk(key_, plaintext, chunk)) return false;  // RNG failure
    }

    uint64_t offset = 0;
    if (!append_at_end(chunk, offset)) return false;
    out.offset = offset;
    out.length = chunk.size();
    return true;
}

bool ChunkStore::read_chunk(ChunkSpan span, std::vector<uint8_t>& out) const noexcept
{
    out.clear();
    if (!span_in_file(span.offset, span.length)) return false;   // OOM guard (unchanged)
    std::vector<uint8_t> disk(span.length);
    if (!read_at(span.offset, disk)) return false;
    if (!framed_) return crypto::decrypt_chunk(key_, disk, out);

    std::vector<uint8_t> framed;
    if (!crypto::decrypt_chunk(key_, disk, framed)) return false;
    return chunk_codec::decode_frame(framed, out);
}

bool ChunkStore::read_chunk(ChunkSpan span, crypto::SecureBytes& out) const noexcept
{
    if (!span_in_file(span.offset, span.length)) return false;
    std::vector<uint8_t> disk(span.length);  // ciphertext is not secret
    if (!read_at(span.offset, disk)) return false;

    const size_t plain_len = crypto::chunk_plaintext_len(disk.size());
    if (!framed_) {
        if (!out.resize(plain_len)) return false;
        if (!crypto::decrypt_chunk_to(key_, disk, out.span())) {
            (void)out.resize(0);
            return false;
        }
        return true;
    }

    crypto::SecureBytes framed;                        // frame = decrypted content: mlock'd
    if (!framed.resize(plain_len)) {
        (void)out.resize(0);  // allocation failure must wipe out
        return false;
    }
    if (!crypto::decrypt_chunk_to(key_, disk, framed.span())) {
        (void)out.resize(0);  // decryption failure must wipe out (critical)
        return false;
    }
    if (!chunk_codec::decode_frame(framed.as_span(), out)) {
        (void)out.resize(0);  // parse rejects may leave out with stale caller content
        return false;
    }
    return true;
}

bool ChunkStore::append_raw(std::span<const uint8_t> bytes, uint64_t& out_offset) noexcept
{
    return append_at_end(bytes, out_offset);
}

bool ChunkStore::read_raw(uint64_t offset, uint64_t length, std::vector<uint8_t>& out) const noexcept
{
    out.clear();
    if (!span_in_file(offset, length)) return false;  // before the allocation
    out.assign(length, 0);
    if (!read_at(offset, out)) {
        out.clear();
        return false;
    }
    return true;
}

bool ChunkStore::sync() noexcept
{
    return fileutil::sync(fp_);
}

} // namespace vault
