#include "media/mem_avio.h"

#ifdef OSV_VENDORED_AV

#include <algorithm>
#include <cstdio>   // SEEK_SET/CUR/END

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
extern "C" {
#include <libavutil/error.h>
#include <libavutil/mem.h>
}
#pragma GCC diagnostic pop

namespace media {

namespace { constexpr int AVIO_BUF = 1 << 16; }   // 64 KiB

MemAvio::MemAvio(std::span<const uint8_t> data) : data_(data)
{
    auto* buffer = static_cast<unsigned char*>(av_malloc(AVIO_BUF));
    if (!buffer) return;                            // ctx_ stays null → valid()==false
    ctx_ = avio_alloc_context(buffer, AVIO_BUF, /*write_flag=*/0, this,
                              &read_cb, /*write=*/nullptr, &seek_cb);
    if (!ctx_) av_free(buffer);
}

MemAvio::~MemAvio()
{
    if (ctx_) {
        av_freep(&ctx_->buffer);                    // FFmpeg may have realloc'd it
        avio_context_free(&ctx_);
    }
}

int MemAvio::read_cb(void* opaque, uint8_t* buf, int buf_size)
{
    auto* self = static_cast<MemAvio*>(opaque);
    if (buf_size <= 0) return 0;

    const size_t available = (self->pos_ < self->data_.size())
                             ? (self->data_.size() - self->pos_)
                             : 0;
    if (available == 0) return AVERROR_EOF;

    const size_t to_copy = std::min(static_cast<size_t>(buf_size), available);
    std::copy_n(self->data_.data() + self->pos_, to_copy, buf);
    self->pos_ += to_copy;
    return static_cast<int>(to_copy);
}

int64_t MemAvio::seek_cb(void* opaque, int64_t offset, int whence)
{
    auto* self = static_cast<MemAvio*>(opaque);
    const auto size = static_cast<int64_t>(self->data_.size());

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
