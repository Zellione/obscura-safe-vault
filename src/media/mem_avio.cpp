#include "media/mem_avio.h"

#ifdef OSV_VENDORED_AV

#include <algorithm>
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

MemAvio::MemAvio(std::span<const uint8_t> data) : data_(data)
{
    auto* buffer = secure_avio_buffer_alloc(buffer_state_);
    if (!buffer) return;
    ctx_ =
        avio_alloc_context(buffer, SECURE_AVIO_BUFFER_SIZE, 0, this, &read_cb, nullptr, &seek_cb);
    if (!ctx_) secure_avio_buffer_discard(buffer, buffer_state_);
}

MemAvio::~MemAvio()
{
    secure_avio_free(ctx_, buffer_state_);
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
