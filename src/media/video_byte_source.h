#pragma once

#include <cstdint>
#include <span>

namespace media {

// Format-neutral plaintext byte source consumed by the FFmpeg AVIO adapter.
// Implementations authenticate data before returning it and retain plaintext
// only in bounded wipe-on-release storage.
class VideoByteSource {
public:
    virtual ~VideoByteSource() = default;
    [[nodiscard]] virtual uint64_t size() const noexcept = 0;
    [[nodiscard]] virtual int64_t read(uint64_t offset, std::span<uint8_t> out) noexcept = 0;
};

}  // namespace media
