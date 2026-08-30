#include "media/ffmpeg_secure.h"

#ifdef OSV_VENDORED_AV

#include <cstddef>
#include <new>

#include "crypto/secure_mem.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/buffer.h>
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
    auto* owner = static_cast<SecureBufferOwner*>(opaque);
    if (!owner) return;
    AVBufferRef* original = owner->original;
    if (original && original->data && original->size > 0) {
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
    delete owner;
}

bool wrap_buffer(AVBufferRef*& slot) noexcept
{
    if (!slot || !slot->data || slot->size == 0) return true;
    auto* owner = new (std::nothrow) SecureBufferOwner;
    if (!owner) return false;
    owner->original = slot;
    owner->locked = crypto::detail::mem_lock(slot->data, slot->size);
    if (!owner->locked) crypto::warn_mlock_failure_once();

    AVBufferRef* wrapper =
        av_buffer_create(slot->data, slot->size, &release_secure_buffer, owner, 0);
    if (!wrapper) {
        if (owner->locked) crypto::detail::mem_unlock(slot->data, slot->size);
        owner->original = nullptr;
        delete owner;
        return false;
    }
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
    const int ret = avcodec_default_get_buffer2(ctx, frame, flags);
    if (ret < 0) return ret;
    if (!secure_frame_storage(frame)) mark_ffmpeg_opaque_storage();
    return 0;
}

void mark_ffmpeg_opaque_storage() noexcept
{
    crypto::mark_opaque_plaintext_seen();
}

}  // namespace media

#endif  // OSV_VENDORED_AV
