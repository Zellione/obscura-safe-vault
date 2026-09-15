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

#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>

#include "media/ffmpeg_secure.h"
#include "media/hwaccel_setting.h"
#include "platform/safe_print.h"

namespace media {

namespace {
// Function-local static, not a namespace-scope global: avoids exposing
// mutable global state directly (SonarQube cpp:S5421) while still giving
// test_only_force_is_hw_format_frame() and is_hw_format_frame() a single
// shared, process-wide override slot.
std::optional<bool>& force_is_hw_format_state()
{
    static std::optional<bool> state;
    return state;
}
}

void test_only_force_is_hw_format_frame(std::optional<bool> force)
{
    force_is_hw_format_state() = force;
}

#if defined(OSV_HWACCEL_VAAPI)

constexpr AVHWDeviceType kHwDeviceType = AV_HWDEVICE_TYPE_VAAPI;
constexpr AVPixelFormat  kHwPixFmt     = AV_PIX_FMT_VAAPI;

namespace {

// Phase 102: cached outcome of the first probe attempt. Once true, every
// subsequent try_attach_hwaccel() call returns the same answer without
// retrying — mirrors the should_warn_mlock_once() / should_warn_mlock_once
// pattern (src/crypto/secure_mem.h) for a best-effort platform capability.
// Phase 104: the test-only force-unavailable flag and the mutex moved in
// here too, so there are no namespace-scope globals left in this TU
// (clears cpp:S5421) and the force-flag is now mutex-protected (was a
// latent thread-safety bug).
struct ProbeCache {
    std::once_flag       once;
    AVBufferRef*         device_ctx        = nullptr;
    HwAccelStatus        status            = HwAccelStatus::NotAttempted;
    std::optional<int>   va_status;
    std::optional<int>   open_errno;
    const char*          vendor            = nullptr;
    bool                 force_unavailable = false;   // test-only override
    std::mutex           mtx;
};
ProbeCache& probe_cache() noexcept
{
    static ProbeCache cache;
    return cache;
}

// The once-per-process device-creation attempt + diagnostic logging.
// Outcome is captured into probe_cache() and mirrored into the F1 status
// (media::hwaccel_status). Thread-safe: every caller gates on the same
// std::once_flag, so parallel VideoDecodeWorker constructors see one
// probe run.
AVBufferRef* cached_device_ctx()
{
    auto& cache = probe_cache();
    std::lock_guard lock(cache.mtx);
    if (cache.device_ctx || cache.status == HwAccelStatus::Unavailable ||
        cache.status == HwAccelStatus::Ok) {
        return cache.device_ctx;
    }
    if (cache.force_unavailable) {
        cache.status = HwAccelStatus::Unavailable;
        return nullptr;
    }

    // Snapshot the levers the user can actually change at runtime, so the
    // single log line names the cause + the knobs.
    const char* libva_driver_name = std::getenv("LIBVA_DRIVER_NAME");
    const char* libva_drivers_path = std::getenv("LIBVA_DRIVERS_PATH");

    if (av_hwdevice_ctx_create(&cache.device_ctx, kHwDeviceType, nullptr, nullptr, 0) < 0) {
        // av_hwdevice_ctx_create fails internally at the same steps our
        // vendor/vaapi-shim surfaces (render-node open / vaInitialize). The
        // FFmpeg call swallows the specific errno / VAStatus; we can't
        // recover the exact code without re-running the probe ourselves.
        // Log the levers + a "see vainfo for details" hint and treat as
        // Unavailable so callers fall back to software silently. The user
        // can run `vainfo` from a shell to see exactly which step failed.
        cache.device_ctx = nullptr;
        cache.status      = HwAccelStatus::Unavailable;
        platform::safe_println(stderr,
            "[HwAccel] VAAPI probe failed: LIBVA_DRIVER_NAME={} LIBVA_DRIVERS_PATH={} — software decode will be used; run `vainfo` for details",
            libva_driver_name ? libva_driver_name : "(unset)",
            libva_drivers_path ? libva_drivers_path : "(unset)");
        return nullptr;
    }

    cache.status = HwAccelStatus::Ok;
    platform::safe_println(stderr,
        "[HwAccel] VAAPI probe: OK; LIBVA_DRIVER_NAME={} LIBVA_DRIVERS_PATH={}",
        libva_driver_name ? libva_driver_name : "(unset)",
        libva_drivers_path ? libva_drivers_path : "(unset)");
    return cache.device_ctx;
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
    // Phase 102: user-toggled overrides. Both skip the entire hwaccel path
    // and keep the worker on a pure software codec context. Tested in
    // tests/media/test_vaapi_shim.cpp (force_software_decode_pref +
    // enable_hardware_decode_pref tests).
    if (!media::enable_hardware_decode()) return false;
    if (media::force_software_decode()) return false;

    if (!decoder_supports_hw(decoder)) {
        // Log once per decoder name per process. "no VAAPI hw_config" is
        // FFmpeg's way of saying the decoder was built without VAAPI
        // support (codec coverage is controlled by FFmpeg's configure-time
        // --enable-hwaccel= list).
        static std::optional<std::string> logged;
        if (const std::string name = decoder ? decoder->name : "(null)";
            !logged.has_value() || *logged != name) {
            platform::safe_println(stderr, "[HwAccel] decoder {} reports no VAAPI hw_config — using software", name);
            logged = name;
        }
        return false;
    }
    const AVBufferRef* dev = cached_device_ctx();
    if (!dev) return false;
    AVBufferRef* ref = av_buffer_ref(dev);
    if (!ref) return false;
    ctx->hw_device_ctx = ref;
    ctx->get_format    = pick_hw_format;
    return true;
}

void test_only_force_hwaccel_unavailable(bool force)
{
    auto& cache = probe_cache();
    std::lock_guard lock(cache.mtx);
    cache.force_unavailable = force;
    if (cache.device_ctx) av_buffer_unref(&cache.device_ctx);
    cache.status = HwAccelStatus::NotAttempted;
    cache.vendor = nullptr;
}

bool transfer_hw_frame(const AVFrame* frame, AVFrame* sw_frame)
{
    if (frame->format != kHwPixFmt) return false;
    av_frame_unref(sw_frame);
    if (av_hwframe_transfer_data(sw_frame, frame, 0) < 0) return false;
    if (!secure_frame_storage(sw_frame)) mark_ffmpeg_opaque_storage();
    // av_hwframe_transfer_data() only copies pixel data, not frame
    // properties — publish_decoded_frame() reads best_effort_timestamp.
    sw_frame->best_effort_timestamp = frame->best_effort_timestamp;
    sw_frame->pts                   = frame->pts;
    return true;
}

bool is_hw_format_frame(const AVFrame* frame)
{
    return force_is_hw_format_state().value_or(frame->format == kHwPixFmt);
}

HwAccelStatus hwaccel_status() noexcept
{
    auto& cache = probe_cache();
    std::lock_guard lock(cache.mtx);
    return cache.status;
}

#else  // no OSV_HWACCEL_VAAPI compiled in this build

bool try_attach_hwaccel(AVCodecContext*, const AVCodec*) { return false; }
void test_only_force_hwaccel_unavailable(bool) { /* no OSV_HWACCEL_VAAPI macro compiled in; nothing to reset */ }
bool transfer_hw_frame(const AVFrame*, AVFrame*) { return false; }

bool is_hw_format_frame(const AVFrame*)
{
    return force_is_hw_format_state().value_or(false);   // no OSV_HWACCEL_VAAPI macro compiled in; frames are always software format
}

HwAccelStatus hwaccel_status() noexcept { return HwAccelStatus::NotAttempted; }

#endif

} // namespace media

#endif // OSV_VENDORED_AV
