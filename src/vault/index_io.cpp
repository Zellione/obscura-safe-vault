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

bool serialize_plain_index(const IndexIoContext& ctx,
                           std::vector<uint8_t>& out)
{
    // Serialize the index (tree + saved searches + settings) using the 4-arg form.
    serialize_index(ctx.root_, ctx.saved_searches_, ctx.settings_, out);

    // Phase 26: framed vaults compress the index blob with the same codec.
    if (framed_chunks(ctx.header_)) {
        std::vector<uint8_t> framed;
        if (!chunk_codec::encode_frame(out, framed)) return false;
        out = std::move(framed);
    }

    return true;
}

VaultResult commit_plain_blob(IndexIoContext& ctx,
                              std::span<const uint8_t> plain)
{
    using enum VaultResult;

    // Generate a fresh random nonce for this blob.
    std::array<uint8_t, crypto::NONCE_SIZE> nonce{};
    if (!crypto::fill_random(nonce)) return CryptoError;

    // Seal the plaintext blob.
    std::vector<uint8_t> sealed;
    crypto::seal(ctx.master_key_.as_span(), nonce, plain, sealed);

    // Phase A: append the new index blob and make it durable.
    ChunkStore store(ctx.fp_, ctx.master_key_.as_span(), framed_chunks(ctx.header_));
    uint64_t offset = 0;
    if (!store.append_raw(sealed, offset)) return IoError;
    if (!store.sync())                     return IoError;

    // Phase 50: guard header-slot mutations under header_mutex_ (if provided).
    // The caller holds the vault write mutex; this additional header mutex
    // protects against concurrent reads of slot fields on the main thread.
    if (ctx.header_mutex_) {
        std::lock_guard lk(*ctx.header_mutex_);
        const uint8_t inactive = ctx.header_.active_slot == 0 ? 1 : 0;
        ctx.header_.slot[inactive] = IndexSlot{.offset = offset,
                                               .length = sealed.size(),
                                               .nonce  = nonce};

        // Phase B: persist the new slot pointer with active_slot still pointing at the
        // old index — both slots are now valid on disk.
        if (!write_header(ctx.fp_, ctx.header_)) return IoError;

        // Phase C: flip active_slot. This is the atomic commit point; a crash before
        // it leaves the previous index in force, after it the new one.
        ctx.header_.active_slot = inactive;
        if (!write_header(ctx.fp_, ctx.header_)) return IoError;
    } else {
        // No header mutex provided (compatibility path for old code paths).
        const uint8_t inactive = ctx.header_.active_slot == 0 ? 1 : 0;
        ctx.header_.slot[inactive] = IndexSlot{.offset = offset,
                                               .length = sealed.size(),
                                               .nonce  = nonce};

        // Phase B: persist the new slot pointer with active_slot still pointing at the
        // old index — both slots are now valid on disk.
        if (!write_header(ctx.fp_, ctx.header_)) return IoError;

        // Phase C: flip active_slot. This is the atomic commit point; a crash before
        // it leaves the previous index in force, after it the new one.
        ctx.header_.active_slot = inactive;
        if (!write_header(ctx.fp_, ctx.header_)) return IoError;
    }

    return Ok;
}

VaultResult commit_index(IndexIoContext& ctx)
{
    using enum VaultResult;

    // Serialize the plaintext index blob.
    std::vector<uint8_t> blob;
    if (!serialize_plain_index(ctx, blob)) return CryptoError;

    // Seal and commit the blob in a crash-safe 3-phase swap.
    return commit_plain_blob(ctx, blob);
}

} // namespace vault::index_io
