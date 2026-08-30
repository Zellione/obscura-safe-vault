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
#include <libavutil/mem.h>
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "crypto/secure_mem.h"
#include "media/ffmpeg_secure.h"

namespace media {

namespace { constexpr int AVIO_BUF = 1 << 16; }   // 64 KiB

ChunkAvio::ChunkAvio(VideoSource source) : source_(std::move(source))
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

ChunkAvio::~ChunkAvio()
{
    if (ctx_) {
        auto* final_buffer = ctx_->buffer;
        const size_t final_size =
            ctx_->buffer_size > 0 ? static_cast<size_t>(ctx_->buffer_size) : 0;
        if (final_buffer == initial_buffer_) {
            crypto_wipe(final_buffer, final_size);
            if (buffer_locked_) crypto::detail::mem_unlock(final_buffer, final_size);
        } else {
            // libavformat is permitted to replace a custom AVIO buffer. The
            // old allocation has already passed through opaque library code;
            // release our stale page-lock registration, secure the actual
            // final pointer for its wipe, and report the boundary in F1.
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
