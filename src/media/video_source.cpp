#include "media/video_source.h"

#include <algorithm>
#include <cstring>

#include "vault/vault.h"   // for the friend factory's access to fp_ / master_key_

namespace media {

VideoSource::VideoSource(std::FILE* fp, std::span<const uint8_t, crypto::KEY_SIZE> key,
                         const vault::VideoMeta& meta)
    : store_(fp, key), chunks_(meta.chunks), chunk_size_(meta.chunk_size),
      total_size_(meta.orig_size) {}

int64_t VideoSource::read(uint64_t offset, std::span<uint8_t> dst) noexcept
{
    if (offset >= total_size_ || chunk_size_ == 0) return 0;
    uint64_t remaining = std::min<uint64_t>(dst.size(), total_size_ - offset);
    uint64_t written   = 0;
    while (remaining > 0) {
        const uint64_t idx    = offset / chunk_size_;
        const uint64_t within = offset % chunk_size_;
        if (idx >= chunks_.size()) break;                  // metadata/size mismatch
        if (cached_index_ != static_cast<int64_t>(idx)) {
            if (!store_.read_chunk({chunks_[idx].offset, chunks_[idx].length}, cache_)) {
                cached_index_ = -1;
                return -1;                                  // auth/decrypt failure
            }
            cached_index_ = static_cast<int64_t>(idx);
        }
        if (within >= cache_.size()) break;                 // corrupt mapping guard
        const uint64_t take = std::min<uint64_t>(remaining, cache_.size() - within);
        std::memcpy(dst.data() + written, cache_.data() + within, take);
        written   += take;
        offset    += take;
        remaining -= take;
    }
    return static_cast<int64_t>(written);
}

VideoSource VideoSource::open(const vault::Vault& v, const vault::IndexNode& node)
{
    return VideoSource(v.fp_, v.master_key_.as_span(), node.vmeta);
}

} // namespace media
