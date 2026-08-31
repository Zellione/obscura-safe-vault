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

// Phase 99 (OSV-AUD-004): the AD the sealed index blob is bound to. Owner =
// the immutable per-vault vault_id (an anonymous per-vault identity that does
// NOT change on change_password); a zero record id; domain Index. Gated on the
// header flag so legacy vaults stay contextless until the migration flips the
// flag at the very end.
std::span<const uint8_t> index_blob_ad(const Header& h,
                                       std::array<uint8_t, crypto::AD_SIZE>& scratch) noexcept
{
    if (!context_bound_chunks(h)) return {};
    crypto::ChunkTag t;
    t.domain        = crypto::ChunkDomain::Index;
    t.owner         = h.vault_id;
    t.context_bound = true;
    scratch         = crypto::build_chunk_ad(t);
    return scratch;
}

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

bool serialize_plain_index(const IndexIoContext& ctx, crypto::WipingBytes& out)
{
    // Serialize the index (tree + saved searches + settings) using the 4-arg form.
    serialize_index(ctx.root_, ctx.saved_searches_, ctx.settings_, out);

    // Phase 26: framed vaults compress the index blob with the same codec.
    if (framed_chunks(ctx.header_)) {
        crypto::WipingBytes framed;
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
    std::array<uint8_t, crypto::AD_SIZE> ad_scratch{};
    const std::span<const uint8_t> ad = index_blob_ad(ctx.header_, ad_scratch);
    std::vector<uint8_t> sealed;
    if (!crypto::seal(ctx.master_key_.as_span(), nonce, plain, sealed, ad)) return CryptoError;

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
    crypto::WipingBytes blob;
    if (!serialize_plain_index(ctx, blob)) return CryptoError;

    // WipingBytes covers success, every early return, and old allocations
    // released while the serializer grows/compresses the blob.
    return commit_plain_blob(ctx, blob);
}

VaultResult commit_plain_blob_at(IndexIoContext& ctx, std::span<const uint8_t> plain,
                                 uint64_t offset)
{
    using enum VaultResult;
    std::array<uint8_t, crypto::NONCE_SIZE> nonce{};
    if (!crypto::fill_random(nonce)) return CryptoError;
    std::array<uint8_t, crypto::AD_SIZE> ad_scratch{};
    const std::span<const uint8_t> ad = index_blob_ad(ctx.header_, ad_scratch);
    std::vector<uint8_t> sealed;
    if (!crypto::seal(ctx.master_key_.as_span(), nonce, plain, sealed, ad)) return CryptoError;

    ChunkStore store(ctx.fp_, ctx.master_key_.as_span(), framed_chunks(ctx.header_));
    if (!store.write_raw_at(offset, sealed)) return IoError;
    if (!store.sync()) return IoError;
    return swap_slots(ctx, offset, sealed.size(), nonce);
}

}  // namespace vault::index_io
