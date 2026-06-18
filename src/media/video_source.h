#pragma once

// On-demand, decrypt-as-you-go view of a vault video's plaintext byte stream.
// FFmpeg-free; ChunkAvio wraps it for libav. Borrows the unlocked vault's file
// handle + master key — valid only while that vault outlives this source.

#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

#include "crypto/crypto.h"        // KEY_SIZE
#include "crypto/secure_mem.h"    // SecureBytes
#include "vault/chunk_store.h"    // ChunkStore, ChunkSpan
#include "vault/index.h"          // VideoMeta, VideoChunk

namespace vault { class Vault; }   // IndexNode/VideoMeta come from vault/index.h above

namespace media {

class VideoSource {
public:
    VideoSource(VideoSource&&) noexcept            = default;
    VideoSource& operator=(VideoSource&&) noexcept = default;

    [[nodiscard]] uint64_t size() const noexcept { return total_size_; }

    // Read up to dst.size() bytes from logical `offset`. Returns bytes read
    // (0 at/after EOF) or -1 on auth/decrypt failure (a wiped cache is left).
    [[nodiscard]] int64_t read(uint64_t offset, std::span<uint8_t> dst) noexcept;

    // Factory: create a VideoSource from an unlocked vault and a video node.
    // The source borrows the vault's file handle and master key — valid only
    // while the vault outlives the source.
    [[nodiscard]] static VideoSource open(const vault::Vault& v, const vault::IndexNode& node);

private:
    VideoSource(std::FILE* fp, std::span<const uint8_t, crypto::KEY_SIZE> key,
                const vault::VideoMeta& meta);

    vault::ChunkStore              store_;
    std::vector<vault::VideoChunk> chunks_;
    uint32_t                       chunk_size_ = 0;
    uint64_t                       total_size_ = 0;
    crypto::SecureBytes            cache_;          // currently-decrypted chunk
    int64_t                        cached_index_ = -1;
};

} // namespace media
