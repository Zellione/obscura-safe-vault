#include "media/chunk_avio.h"

#ifdef OSV_VENDORED_AV

#include <cstdio>   // SEEK_SET/CUR/END

extern "C" {
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

namespace media {

namespace { constexpr int AVIO_BUF = 1 << 16; }   // 64 KiB

ChunkAvio::ChunkAvio(VideoSource source) : source_(std::move(source))
{
    auto* buffer = static_cast<unsigned char*>(av_malloc(AVIO_BUF));
    if (!buffer) return;                            // ctx_ stays null → valid()==false
    ctx_ = avio_alloc_context(buffer, AVIO_BUF, /*write_flag=*/0, this,
                              &read_cb, /*write=*/nullptr, &seek_cb);
    if (!ctx_) av_free(buffer);
}

ChunkAvio::~ChunkAvio()
{
    if (ctx_) {
        av_freep(&ctx_->buffer);                    // FFmpeg may have realloc'd it
        avio_context_free(&ctx_);
    }
}

int ChunkAvio::read_cb(void* opaque, uint8_t* buf, int buf_size)
{
    auto* self = static_cast<ChunkAvio*>(opaque);
    if (buf_size <= 0) return 0;
    const int64_t n = self->source_.read(self->pos_,
                                         std::span<uint8_t>(buf, static_cast<size_t>(buf_size)));
    if (n < 0)  return AVERROR(EIO);                // auth/decrypt failure
    if (n == 0) return AVERROR_EOF;
    self->pos_ += static_cast<uint64_t>(n);
    return static_cast<int>(n);
}

int64_t ChunkAvio::seek_cb(void* opaque, int64_t offset, int whence)
{
    auto* self = static_cast<ChunkAvio*>(opaque);
    const int64_t size = static_cast<int64_t>(self->source_.size());

    // Handle AVSEEK_SIZE first (special query, not a real seek).
    if ((whence & AVSEEK_SIZE) == AVSEEK_SIZE) {
        return size;
    }

    // Handle standard SEEK_* constants (mask AVSEEK_FORCE which may be OR'd in).
    whence &= ~AVSEEK_FORCE;

    switch (whence) {
        case SEEK_SET: self->pos_ = static_cast<uint64_t>(offset); break;
        case SEEK_CUR: self->pos_ = static_cast<uint64_t>(static_cast<int64_t>(self->pos_) + offset); break;
        case SEEK_END: self->pos_ = static_cast<uint64_t>(size + offset); break;
        default:       return AVERROR(EINVAL);
    }
    return static_cast<int64_t>(self->pos_);
}

} // namespace media

#endif // OSV_VENDORED_AV
