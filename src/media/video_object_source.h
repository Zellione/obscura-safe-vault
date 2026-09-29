#pragma once

#include "media/video_byte_source.h"
#include "vault/v3_object_store.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>

namespace media {

// Authenticated random-access plaintext view of one v3 OriginalVideo object.
// The two-frame cache is page-locked best effort and wiped on eviction.
class VideoObjectSource final : public VideoByteSource {
public:
    struct OpenResult {
        vault::v3::ObjectStatus status = vault::v3::ObjectStatus::IoError;
        std::unique_ptr<VideoObjectSource> source;
    };

    [[nodiscard]] static OpenResult
    open(const vault::v3::VaultRoot& root,
         std::span<const uint8_t, crypto::KEY_SIZE> master_key,
         const vault::v3::ObjectInfo& expected) noexcept;

    [[nodiscard]] uint64_t size() const noexcept override;
    [[nodiscard]] int64_t read(uint64_t offset, std::span<uint8_t> out) noexcept override;
    void cancel() noexcept;
    [[nodiscard]] bool cancelled() const noexcept;
    [[nodiscard]] size_t cached_frame_count() const noexcept;
    [[nodiscard]] vault::v3::ObjectStatus last_status() const noexcept;
    [[nodiscard]] uint32_t last_error_frame() const noexcept;

private:
    struct CacheEntry {
        uint32_t index = 0;
        bool valid = false;
        uint64_t stamp = 0;
        crypto::SecureBytes bytes;
    };

    explicit VideoObjectSource(std::unique_ptr<vault::v3::ObjectReader> reader) noexcept;
    [[nodiscard]] CacheEntry* load_frame(uint32_t index) noexcept;
    void clear_cache() noexcept;

    std::unique_ptr<vault::v3::ObjectReader> reader_;
    mutable std::mutex mutex_;
    std::array<CacheEntry, 2> cache_{};
    uint64_t stamp_ = 0;
    std::atomic<bool> cancelled_{false};
    vault::v3::ObjectStatus last_status_ = vault::v3::ObjectStatus::Ok;
    uint32_t last_error_frame_ = 0;
    bool diagnostic_reported_ = false;
};

}  // namespace media
