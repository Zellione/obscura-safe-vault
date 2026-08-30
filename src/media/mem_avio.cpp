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
#include <libavutil/mem.h>
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "crypto/secure_mem.h"
#include "media/ffmpeg_secure.h"

namespace media {

namespace { constexpr int AVIO_BUF = 1 << 16; }   // 64 KiB

MemAvio::MemAvio(std::span<const uint8_t> data) : data_(data)
{
    auto* buffer = static_cast<unsigned char*>(av_malloc(AVIO_BUF));
    if (!buffer) return;                            // ctx_ stays null → valid()==false
    initial_buffer_ = buffer;
    buffer_locked_ = crypto::detail::mem_lock(buffer, AVIO_BUF);
    if (!buffer_locked_) crypto::warn_mlock_failure_once();
    ctx_ = avio_alloc_context(buffer, AVIO_BUF, /*write_flag=*/0, this,
                              &read_cb, /*write=*/nullptr, &seek_cb);
    if (!ctx_) {
        crypto_wipe(buffer, AVIO_BUF);
        if (buffer_locked_) crypto::detail::mem_unlock(buffer, AVIO_BUF);
        crypto::detail::record_wipe_for_tests(std::as_bytes(std::span(buffer, size_t{AVIO_BUF})));
        av_free(buffer);
        initial_buffer_ = nullptr;
        buffer_locked_ = false;
    }
}

MemAvio::~MemAvio()
{
    if (ctx_) {
        auto* final_buffer = ctx_->buffer;
        const size_t final_size =
            ctx_->buffer_size > 0 ? static_cast<size_t>(ctx_->buffer_size) : 0;
        if (final_buffer == initial_buffer_) {
            crypto_wipe(final_buffer, final_size);
            if (buffer_locked_) crypto::detail::mem_unlock(final_buffer, final_size);
        } else {
            if (buffer_locked_) crypto::detail::mem_forget_lock(initial_buffer_, AVIO_BUF);
            mark_ffmpeg_opaque_storage();
            const bool final_locked = crypto::detail::mem_lock(final_buffer, final_size);
            crypto_wipe(final_buffer, final_size);
            if (final_locked) crypto::detail::mem_unlock(final_buffer, final_size);
        }
        crypto::detail::record_wipe_for_tests(std::as_bytes(std::span(final_buffer, final_size)));
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
