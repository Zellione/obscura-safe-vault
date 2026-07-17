#ifdef OSV_VENDORED_AV

#include "media/hw_accel.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <mutex>

namespace media {

#if defined(OSV_HWACCEL_D3D11VA) || defined(OSV_HWACCEL_VAAPI)

#if defined(OSV_HWACCEL_D3D11VA)
constexpr AVHWDeviceType kHwDeviceType = AV_HWDEVICE_TYPE_D3D11VA;
constexpr AVPixelFormat  kHwPixFmt     = AV_PIX_FMT_D3D11;
#elif defined(OSV_HWACCEL_VAAPI)
constexpr AVHWDeviceType kHwDeviceType = AV_HWDEVICE_TYPE_VAAPI;
constexpr AVPixelFormat  kHwPixFmt     = AV_PIX_FMT_VAAPI;
#endif

namespace {
bool         g_force_unavailable = false;
bool         g_device_attempted  = false;
AVBufferRef* g_device_ctx        = nullptr;
std::mutex   g_device_mutex;

// Attempts real hw device creation exactly once per process; every call
// after the first returns the cached outcome — mirrors
// should_warn_mlock_once()'s cache-the-outcome pattern (src/crypto/secure_mem.h)
// for a best-effort platform capability. g_device_mutex guards all reads/
// writes of the three globals above (not just this function) since
// test_only_force_hwaccel_unavailable() also mutates them from test code.
AVBufferRef* cached_device_ctx()
{
    std::lock_guard lock(g_device_mutex);
    if (g_device_attempted) return g_device_ctx;
    g_device_attempted = true;
    if (g_force_unavailable) return nullptr;
    if (av_hwdevice_ctx_create(&g_device_ctx, kHwDeviceType, nullptr, nullptr, 0) < 0)
        g_device_ctx = nullptr;
    return g_device_ctx;
}

bool decoder_supports_hw(const AVCodec* decoder)
{
    for (int i = 0;; ++i) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(decoder, i);
        if (!config) return false;
        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
            config->device_type == kHwDeviceType)
            return true;
    }
}

enum AVPixelFormat pick_hw_format(AVCodecContext*, const enum AVPixelFormat* fmts)
{
    for (const enum AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
        if (*p == kHwPixFmt) return *p;
    return AV_PIX_FMT_NONE;
}
} // namespace

bool try_attach_hwaccel(AVCodecContext* ctx, const AVCodec* decoder)
{
    if (!decoder_supports_hw(decoder)) return false;
    AVBufferRef* dev = cached_device_ctx();
    if (!dev) return false;
    AVBufferRef* ref = av_buffer_ref(dev);
    if (!ref) return false;
    ctx->hw_device_ctx = ref;
    ctx->get_format    = pick_hw_format;
    return true;
}

void test_only_force_hwaccel_unavailable(bool force)
{
    std::lock_guard lock(g_device_mutex);
    g_force_unavailable = force;
    g_device_attempted  = false;   // re-evaluate on the next try_attach_hwaccel() call
    if (g_device_ctx) av_buffer_unref(&g_device_ctx);
}

bool transfer_hw_frame(const AVFrame* frame, AVFrame* sw_frame)
{
    if (frame->format != kHwPixFmt) return false;
    av_frame_unref(sw_frame);
    if (av_hwframe_transfer_data(sw_frame, frame, 0) < 0) return false;
    // av_hwframe_transfer_data() only copies pixel data, not frame
    // properties — publish_decoded_frame() reads best_effort_timestamp.
    sw_frame->best_effort_timestamp = frame->best_effort_timestamp;
    sw_frame->pts                   = frame->pts;
    return true;
}

#else  // no OSV_HWACCEL_* compiled in this build (Linux, until Part 2)

bool try_attach_hwaccel(AVCodecContext*, const AVCodec*) { return false; }
void test_only_force_hwaccel_unavailable(bool) {}
bool transfer_hw_frame(const AVFrame*, AVFrame*) { return false; }

#endif

} // namespace media

#endif // OSV_VENDORED_AV
