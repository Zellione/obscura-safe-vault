#include "index_io.h"

#include <array>
#include <cstring>

#include "chunk_codec.h"
#include "chunk_store.h"
#include "crypto/aead.h"
#include "crypto/random.h"
#include "file_util.h"

namespace vault::index_io {

namespace {

// Phases B+C of the 3-phase swap: point the inactive slot at the sealed blob,
// persist, then flip active_slot (the atomic commit point).
VaultResult swap_slots(IndexIoContext& ctx, uint64_t offset, uint64_t sealed_len,
                       const std::array<uint8_t, crypto::NONCE_SIZE>& nonce)
{
    using enum VaultResult;
    auto do_swap = [&]() {
        const uint8_t inactive = ctx.header_.active_slot == 0 ? 1 : 0;
        ctx.header_.slot[inactive] =
            IndexSlot{.offset = offset, .length = sealed_len, .nonce = nonce};
        // Phase B: persist slot allocation.
        if (!write_header(ctx.fp_, ctx.header_)) return IoError;
        ctx.header_.active_slot = inactive;
        // Phase C: flip active_slot (atomic commit point).
        if (!write_header(ctx.fp_, ctx.header_)) return IoError;
        return Ok;
    };
    if (ctx.header_mutex_) {
        std::lock_guard lk(*ctx.header_mutex_);
        return do_swap();
    }
    return do_swap();
}

}  // namespace

bool write_header(std::FILE* fp, const Header& h)
{
    std::array<uint8_t, HEADER_SIZE> raw{};
    h.serialize(raw);
    if (!fileutil::seek_to(fp, 0)) return false;
    if (std::fwrite(raw.data(), 1, raw.size(), fp) != raw.size()) return false;
    return fileutil::sync(fp);
}

bool serialize_plain_index(const IndexIoContext& ctx, std::vector<uint8_t>& out)
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

VaultResult commit_plain_blob(IndexIoContext& ctx, std::span<const uint8_t> plain)
{
    using enum VaultResult;
    std::array<uint8_t, crypto::NONCE_SIZE> nonce{};
    if (!crypto::fill_random(nonce)) return CryptoError;
    std::vector<uint8_t> sealed;
    crypto::seal(ctx.master_key_.as_span(), nonce, plain, sealed);

    ChunkStore store(ctx.fp_, ctx.master_key_.as_span(), framed_chunks(ctx.header_));
    uint64_t offset = 0;
    if (!store.append_raw(sealed, offset)) return IoError;
    if (!store.sync()) return IoError;
    return swap_slots(ctx, offset, sealed.size(), nonce);
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

VaultResult commit_plain_blob_at(IndexIoContext& ctx, std::span<const uint8_t> plain,
                                 uint64_t offset)
{
    using enum VaultResult;
    std::array<uint8_t, crypto::NONCE_SIZE> nonce{};
    if (!crypto::fill_random(nonce)) return CryptoError;
    std::vector<uint8_t> sealed;
    crypto::seal(ctx.master_key_.as_span(), nonce, plain, sealed);

    ChunkStore store(ctx.fp_, ctx.master_key_.as_span(), framed_chunks(ctx.header_));
    if (!store.write_raw_at(offset, sealed)) return IoError;
    if (!store.sync()) return IoError;
    return swap_slots(ctx, offset, sealed.size(), nonce);
}

}  // namespace vault::index_io
