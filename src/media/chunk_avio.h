#pragma once

#ifdef OSV_VENDORED_AV

// Wraps a VideoSource in an FFmpeg AVIOContext (read + seek; never writes, never
// opens a file). The decoder (PR4) sets AVFormatContext::pb = ctx(). No bytes
// touch disk: the context is an in-memory buffer over decrypt-on-demand chunks.

#include <cstdint>
#include "media/video_source.h"

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

class ChunkAvio {
public:
    explicit ChunkAvio(VideoSource source);   // takes ownership of the source
    ~ChunkAvio();
    ChunkAvio(const ChunkAvio&)            = delete;
    ChunkAvio& operator=(const ChunkAvio&) = delete;

    [[nodiscard]] AVIOContext* ctx()   const noexcept { return ctx_; }
    [[nodiscard]] bool         valid() const noexcept { return ctx_ != nullptr; }

private:
    static int     read_cb(void* opaque, uint8_t* buf, int buf_size);
    static int64_t seek_cb(void* opaque, int64_t offset, int whence);

    VideoSource  source_;
    uint64_t     pos_ = 0;
    AVIOContext* ctx_ = nullptr;
};

} // namespace media

#endif // OSV_VENDORED_AV
