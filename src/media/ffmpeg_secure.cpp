#include "media/ffmpeg_secure.h"

#ifdef OSV_VENDORED_AV

#include <cstddef>
#include <memory>

#include "crypto/secure_mem.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avio.h>
#include <libavutil/buffer.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace media {
namespace {

struct SecureBufferOwner {
    AVBufferRef* original = nullptr;
    bool locked = false;
};

void release_secure_buffer(void* opaque, uint8_t*) noexcept
{
    std::unique_ptr<SecureBufferOwner> owner(static_cast<SecureBufferOwner*>(opaque));
    if (!owner) return;
    if (AVBufferRef* original = owner->original; original && original->data && original->size > 0) {
        if (av_buffer_get_ref_count(original) == 1) {
            crypto_wipe(original->data, original->size);
            crypto::detail::record_wipe_for_tests(
                std::as_bytes(std::span(original->data, original->size)));
        } else {
            crypto::mark_opaque_plaintext_seen();
        }
        if (owner->locked) crypto::detail::mem_unlock(original->data, original->size);
    }
    av_buffer_unref(&owner->original);
}

bool wrap_buffer(AVBufferRef*& slot) noexcept
{
    if (!slot || !slot->data || slot->size == 0) return true;
    std::unique_ptr<SecureBufferOwner> owner;
    try {
        owner = std::make_unique<SecureBufferOwner>();
    } catch (const std::bad_alloc&) {
        return false;
    }
    owner->original = slot;
    owner->locked = crypto::detail::mem_lock(slot->data, slot->size);
    if (!owner->locked) crypto::warn_mlock_failure_once();

    AVBufferRef* wrapper =
        av_buffer_create(slot->data, slot->size, &release_secure_buffer, owner.get(), 0);
    if (!wrapper) {
        if (owner->locked) crypto::detail::mem_unlock(slot->data, slot->size);
        owner->original = nullptr;
        return false;
    }
    owner.release();
    slot = wrapper;
    return true;
}

bool frame_is_hardware(const AVFrame* frame) noexcept
{
    if (!frame) return false;
    const auto* desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
    return desc && (desc->flags & AV_PIX_FMT_FLAG_HWACCEL) != 0;
}

}  // namespace

uint8_t* secure_avio_buffer_alloc(SecureAvioBufferState& state) noexcept
{
    auto* buffer = static_cast<uint8_t*>(av_malloc(SECURE_AVIO_BUFFER_SIZE));
    if (!buffer) return nullptr;
    state.initial = buffer;
    state.locked = crypto::detail::mem_lock(buffer, SECURE_AVIO_BUFFER_SIZE);
    if (!state.locked) crypto::warn_mlock_failure_once();
    return buffer;
}

void secure_avio_buffer_discard(uint8_t* buffer, SecureAvioBufferState& state) noexcept
{
    if (!buffer) return;
    crypto_wipe(buffer, SECURE_AVIO_BUFFER_SIZE);
    if (state.locked) crypto::detail::mem_unlock(buffer, SECURE_AVIO_BUFFER_SIZE);
    crypto::detail::record_wipe_for_tests(
        std::as_bytes(std::span(buffer, size_t{SECURE_AVIO_BUFFER_SIZE})));
    av_free(buffer);
    state = {};
}

void secure_avio_free(AVIOContext*& ctx, SecureAvioBufferState& state) noexcept
{
    if (!ctx) return;
    auto* final_buffer = ctx->buffer;
    const size_t final_size = ctx->buffer_size > 0 ? static_cast<size_t>(ctx->buffer_size) : 0;
    if (final_buffer == state.initial) {
        crypto_wipe(final_buffer, final_size);
        if (state.locked) crypto::detail::mem_unlock(final_buffer, final_size);
    } else {
        if (state.locked) crypto::detail::mem_forget_lock(state.initial, SECURE_AVIO_BUFFER_SIZE);
        mark_ffmpeg_opaque_storage();
        const bool final_locked = crypto::detail::mem_lock(final_buffer, final_size);
        crypto_wipe(final_buffer, final_size);
        if (final_locked) crypto::detail::mem_unlock(final_buffer, final_size);
    }
    crypto::detail::record_wipe_for_tests(std::as_bytes(std::span(final_buffer, final_size)));
    av_freep(&ctx->buffer);
    avio_context_free(&ctx);
    state = {};
}

bool secure_packet_storage(AVPacket* packet) noexcept
{
    if (!packet || !packet->buf || !packet->data || packet->size <= 0) return true;
    return wrap_buffer(packet->buf);
}

bool secure_frame_storage(AVFrame* frame) noexcept
{
    if (!frame) return false;
    if (frame_is_hardware(frame)) {
        mark_ffmpeg_opaque_storage();
        return false;
    }
    for (AVBufferRef*& buffer : frame->buf)
        if (!wrap_buffer(buffer)) return false;
    for (int i = 0; i < frame->nb_extended_buf; ++i)
        if (!wrap_buffer(frame->extended_buf[i])) return false;
    return true;
}

int secure_get_buffer2(AVCodecContext* ctx, AVFrame* frame, int flags) noexcept
{
    if (const int ret = avcodec_default_get_buffer2(ctx, frame, flags); ret < 0) return ret;
    if (!secure_frame_storage(frame)) mark_ffmpeg_opaque_storage();
    return 0;
}

void mark_ffmpeg_opaque_storage() noexcept
{
    crypto::mark_opaque_plaintext_seen();
}

}  // namespace media

#endif  // OSV_VENDORED_AV
