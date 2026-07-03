#pragma once

#ifdef OSV_VENDORED_AV

// Wraps a plaintext in-memory buffer in an FFmpeg AVIOContext (read + seek; never
// writes, never opens a file). The decoder sets AVFormatContext::pb = ctx().
// Unlike ChunkAvio (which reads from encrypted chunks), MemAvio reads directly from
// a borrowed span, so no chunk decryption is needed.

#include <cstdint>
#include <span>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
extern "C" {
#include <libavformat/avio.h>
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace media {

class MemAvio {
public:
    explicit MemAvio(std::span<const uint8_t> data);
    ~MemAvio();
    MemAvio(const MemAvio&)            = delete;
    MemAvio& operator=(const MemAvio&) = delete;

    [[nodiscard]] AVIOContext* ctx()   const noexcept { return ctx_; }
    [[nodiscard]] bool         valid() const noexcept { return ctx_ != nullptr; }

private:
    static int     read_cb(void* opaque, uint8_t* buf, int buf_size);
    static int64_t seek_cb(void* opaque, int64_t offset, int whence);

    std::span<const uint8_t> data_;
    uint64_t                 pos_ = 0;
    AVIOContext*             ctx_ = nullptr;
};

} // namespace media

#endif // OSV_VENDORED_AV
