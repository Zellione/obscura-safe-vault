#include "chunk_store.h"

#include <new>
#include <stdexcept>

#include "chunk_codec.h"
#include "crypto/aead.h"
#include "crypto/random.h"
#include "file_util.h"
#include "platform/safe_print.h"

namespace vault {

using fileutil::file_size;
using fileutil::seek_end;
using fileutil::seek_to;

crypto::ChunkTag chunk_tag(crypto::ChunkDomain domain, const IndexNode& node,
                           const std::array<uint8_t, crypto::NODE_ID_SIZE>& record,
                           uint32_t sequence) noexcept
{
    crypto::ChunkTag t;
    t.domain        = domain;
    t.owner         = node.node_id;
    t.record        = record;
    t.sequence      = sequence;
    t.context_bound = node.is_video() ? node.vmeta.context_bound : node.meta.context_bound;
    return t;
}

bool ChunkStore::append_at_end(std::span<const uint8_t> bytes, uint64_t& out_offset) noexcept
{
    uint64_t end = 0;
    if (!seek_end(fp_, end)) {
        platform::safe_println(stderr, "[vault::chunk_store] seek to end failed");
        return false;
    }
    if (!bytes.empty() &&
        std::fwrite(bytes.data(), 1, bytes.size(), fp_) != bytes.size()) {
        platform::safe_println(stderr, "[vault::chunk_store] write of {} bytes failed", bytes.size());
        return false;
    }
    // Push the append out of the stdio buffer to the fd. Every caller already
    // flushes right after (staging, commit sync), so this is redundant in
    // production; it exists so a read-back through the SAME handle sees the data
    // now that file_size() is fstat-based (position-independent) and no longer
    // flushes as a side effect of seeking to end.
    if (std::fflush(fp_) != 0) {
        platform::safe_println(stderr, "[vault::chunk_store] flush after append failed");
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

bool ChunkStore::append_chunk(std::span<const uint8_t> plaintext, crypto::ChunkTag& tag,
                              ChunkSpan& out) noexcept
{
    if (tag.context_bound) {
        // A fresh random record id per write: a replayed old record of the same
        // node/role/sequence (e.g. a regenerated thumbnail's dead predecessor)
        // then cannot authenticate — its record id differs.
        if (!crypto::fill_random(tag.record)) return false;
    }
    const std::array<uint8_t, crypto::AD_SIZE> ad_arr = crypto::build_chunk_ad(tag);
    const std::span<const uint8_t> ad =
        tag.context_bound ? std::span<const uint8_t>(ad_arr) : std::span<const uint8_t>{};

    std::vector<uint8_t> chunk;
    if (framed_) {
        // The frame holds (possibly compressed) decrypted content: mlock'd.
        crypto::SecureBytes framed;
        if (!chunk_codec::encode_frame(plaintext, framed)) return false;
        if (!crypto::encrypt_chunk(key_, framed.as_span(), chunk, ad)) return false;
    } else {
        if (!crypto::encrypt_chunk(key_, plaintext, chunk, ad)) return false;  // RNG failure
    }

    uint64_t offset = 0;
    if (!append_at_end(chunk, offset)) return false;
    out.offset = offset;
    out.length = chunk.size();
    return true;
}

bool ChunkStore::read_chunk(ChunkSpan span, const crypto::ChunkTag& tag,
                            std::vector<uint8_t>& out) const noexcept
{
    out.clear();
    if (!span_in_file(span.offset, span.length)) return false;   // OOM guard (unchanged)
    if (span.length > std::vector<uint8_t>{}.max_size()) return false;
    std::vector<uint8_t> disk;
    try {
        disk.resize(static_cast<size_t>(span.length));
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
    if (!read_at(span.offset, disk)) return false;
    const std::array<uint8_t, crypto::AD_SIZE> ad_arr = crypto::build_chunk_ad(tag);
    const std::span<const uint8_t> ad =
        tag.context_bound ? std::span<const uint8_t>(ad_arr) : std::span<const uint8_t>{};
    if (!framed_) return crypto::decrypt_chunk(key_, disk, out, ad);

    std::vector<uint8_t> framed;
    if (!crypto::decrypt_chunk(key_, disk, framed, ad)) return false;
    return chunk_codec::decode_frame(framed, out);
}

bool ChunkStore::read_chunk(ChunkSpan span, const crypto::ChunkTag& tag,
                            crypto::SecureBytes& out) const noexcept
{
    // Every failure path must discard stale plaintext from a prior read.
    (void)out.resize(0);
    if (!span_in_file(span.offset, span.length)) return false;
    if (span.length > std::vector<uint8_t>{}.max_size()) return false;
    std::vector<uint8_t> disk;  // ciphertext is not secret
    try {
        disk.resize(static_cast<size_t>(span.length));
    } catch (const std::bad_alloc&) {
        (void)out.resize(0);
        return false;
    } catch (const std::length_error&) {
        (void)out.resize(0);
        return false;
    }
    if (!read_at(span.offset, disk)) return false;

    const std::array<uint8_t, crypto::AD_SIZE> ad_arr = crypto::build_chunk_ad(tag);
    const std::span<const uint8_t> ad =
        tag.context_bound ? std::span<const uint8_t>(ad_arr) : std::span<const uint8_t>{};

    const size_t plain_len = crypto::chunk_plaintext_len(disk.size());
    if (!framed_) {
        if (!out.resize(plain_len)) return false;
        if (!crypto::decrypt_chunk_to(key_, disk, out.span(), ad)) {
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
    if (!crypto::decrypt_chunk_to(key_, disk, framed.span(), ad)) {
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
    if (length > out.max_size()) return false;
    try {
        out.assign(static_cast<size_t>(length), 0);
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
    if (!read_at(offset, out)) {
        out.clear();
        return false;
    }
    return true;
}

bool ChunkStore::write_raw_at(uint64_t offset, std::span<const uint8_t> bytes) noexcept
{
    if (bytes.empty()) return true;
    if (!fileutil::seek_to(fp_, offset)) return false;
    if (std::fwrite(bytes.data(), 1, bytes.size(), fp_) != bytes.size()) return false;
    // Keep read-after-write on the same handle coherent (mirrors append_at_end's
    // post-write fflush — see file_util.h's position-independence note).
    return std::fflush(fp_) == 0;
}

bool ChunkStore::sync() noexcept
{
    return fileutil::sync(fp_);
}

} // namespace vault
