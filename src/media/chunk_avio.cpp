#include "media/chunk_avio.h"

#ifdef OSV_VENDORED_AV

#include <cstdio>   // SEEK_SET/CUR/END

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
extern "C" {
#include <libavutil/error.h>
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace media {

ChunkAvio::ChunkAvio(VideoSource source) : source_(std::move(source))
{
    ctx_ = secure_avio_alloc(this, &read_cb, &seek_cb, buffer_state_);
}

ChunkAvio::~ChunkAvio()
{
    secure_avio_free(ctx_, buffer_state_);
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
    const auto size = static_cast<int64_t>(self->source_.size());

    // Handle AVSEEK_SIZE first (special query, not a real seek).
    if ((whence & AVSEEK_SIZE) == AVSEEK_SIZE) {
        return size;
    }

    // Handle standard SEEK_* constants (mask AVSEEK_FORCE which may be OR'd in).
    whence &= ~AVSEEK_FORCE;

    int64_t new_pos;
    switch (whence) {
        case SEEK_SET: new_pos = offset; break;
        case SEEK_CUR: new_pos = static_cast<int64_t>(self->pos_) + offset; break;
        case SEEK_END: new_pos = size + offset; break;
        default:       return AVERROR(EINVAL);
    }
    if (new_pos < 0) return AVERROR(EINVAL);   // can't seek before the start of the stream
    self->pos_ = static_cast<uint64_t>(new_pos);
    return new_pos;
}

} // namespace media

#endif // OSV_VENDORED_AV
