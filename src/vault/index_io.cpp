#include "index_io.h"

#include <array>
#include <cstring>

#include "crypto/aead.h"
#include "crypto/random.h"
#include "chunk_codec.h"
#include "chunk_store.h"
#include "file_util.h"

namespace vault::index_io {

bool write_header(std::FILE* fp, const Header& h)
{
    std::array<uint8_t, HEADER_SIZE> raw{};
    h.serialize(raw);
    if (!fileutil::seek_to(fp, 0)) return false;
    if (std::fwrite(raw.data(), 1, raw.size(), fp) != raw.size()) return false;
    return fileutil::sync(fp);
}

VaultResult commit_index(IndexIoContext& ctx)
{
    using enum VaultResult;

    // Serialise + seal the index (tree + saved searches + settings) with a fresh random nonce.
    std::vector<uint8_t> blob;
    serialize_index(ctx.root_, ctx.saved_searches_, ctx.settings_, blob);

    // Phase 26: framed vaults compress the index blob with the same codec.
    std::vector<uint8_t> framed;
    if (framed_chunks(ctx.header_)) {
        if (!chunk_codec::encode_frame(blob, framed)) return CryptoError;
        blob = std::move(framed);
    }

    std::array<uint8_t, crypto::NONCE_SIZE> nonce{};
    if (!crypto::fill_random(nonce)) return CryptoError;

    std::vector<uint8_t> sealed;
    crypto::seal(ctx.master_key_.as_span(), nonce, blob, sealed);

    // Step A: append the new index blob and make it durable.
    ChunkStore store(ctx.fp_, ctx.master_key_.as_span(), framed_chunks(ctx.header_));
    uint64_t offset = 0;
    if (!store.append_raw(sealed, offset)) return IoError;
    if (!store.sync())                     return IoError;

    const uint8_t inactive = ctx.header_.active_slot == 0 ? 1 : 0;
    ctx.header_.slot[inactive] = IndexSlot{.offset = offset,
                                           .length = sealed.size(),
                                           .nonce  = nonce};

    // Step B: persist the new slot pointer with active_slot still pointing at the
    // old index — both slots are now valid on disk.
    if (!write_header(ctx.fp_, ctx.header_)) return IoError;

    // Step C: flip active_slot. This is the atomic commit point; a crash before
    // it leaves the previous index in force, after it the new one.
    ctx.header_.active_slot = inactive;
    if (!write_header(ctx.fp_, ctx.header_)) return IoError;

    return Ok;
}

} // namespace vault::index_io
