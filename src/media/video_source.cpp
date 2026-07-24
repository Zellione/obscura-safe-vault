#include "media/video_source.h"

#include <algorithm>
#include <cstring>

#include "vault/header.h"   // for framed_chunks
#include "vault/vault.h"   // for the friend factory's access to read_fp_ / master_key_

namespace media {

VideoSource::VideoSource(std::FILE* fp, std::span<const uint8_t, crypto::KEY_SIZE> key,
                         const vault::VideoMeta& meta, bool framed)
    : store_(fp, key, framed), chunks_(meta.chunks), chunk_size_(meta.chunk_size),
      total_size_(meta.orig_size) {}

// Copy up to one chunk's worth of plaintext covering `offset` into `dst`,
// decrypting that chunk on demand. Returns bytes copied, 0 on a corrupt
// chunk/size mismatch, or -1 on an auth/decrypt failure (cache wiped).
int64_t VideoSource::fill_one(uint64_t offset, std::span<uint8_t> dst) noexcept
{
    const uint64_t idx = offset / chunk_size_;
    if (idx >= chunks_.size()) return 0;                    // metadata/size mismatch
    if (cached_index_ != static_cast<int64_t>(idx)) {
        if (!store_.read_chunk({chunks_[idx].offset, chunks_[idx].length}, cache_)) {
            (void)cache_.resize(0);                         // wipe any stale plaintext
            cached_index_ = -1;
            return -1;                                      // auth/decrypt failure
        }
        cached_index_ = static_cast<int64_t>(idx);
    }
    const uint64_t within = offset % chunk_size_;
    if (within >= cache_.size()) return 0;                  // corrupt mapping guard
    const uint64_t take = std::min<uint64_t>(dst.size(), cache_.size() - within);
    std::memcpy(dst.data(), cache_.data() + within, take);
    return static_cast<int64_t>(take);
}

int64_t VideoSource::read(uint64_t offset, std::span<uint8_t> dst) noexcept
{
    if (offset >= total_size_ || chunk_size_ == 0) return 0;
    const uint64_t want = std::min<uint64_t>(dst.size(), total_size_ - offset);
    uint64_t written = 0;
    while (written < want) {
        const int64_t n = fill_one(offset + written, dst.subspan(written, want - written));
        if (n < 0) return -1;                               // auth/decrypt failure
        if (n == 0) break;                                  // corrupt mapping — stop
        written += static_cast<uint64_t>(n);
    }
    return static_cast<int64_t>(written);
}

VideoSource VideoSource::open(const vault::Vault& v, const vault::IndexNode& node)
{
    return VideoSource(v.read_fp_, v.master_key_.as_span(), node.vmeta, framed_chunks(v.header_));
}

} // namespace media
