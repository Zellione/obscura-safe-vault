#pragma once

#ifdef OSV_VENDORED_AV

#include <optional>

struct AVCodecContext;
struct AVCodec;
struct AVFrame;

namespace media {

// Attempts to attach a process-wide hardware decode device context to `ctx`
// for `decoder`. Returns true if a hw_device_ctx + get_format callback were
// installed — i.e. this platform build has hwaccel compiled in
// (OSV_HWACCEL_D3D11VA today; OSV_HWACCEL_VAAPI is a future phase),
// `decoder` advertises support for this platform's hw device type, and
// device creation (attempted once per process, cached) succeeded. Compiles
// to an always-false stub when no OSV_HWACCEL_* macro is defined, so
// callers never need their own #ifdef.
//
// Returning true is an OPPORTUNITY, not a guarantee: the caller must still
// treat a subsequent hard decode failure as expected and fall back to a
// fresh software-only codec context (see
// VideoDecodeWorker::reopen_software_only()).
[[nodiscard]] bool try_attach_hwaccel(AVCodecContext* ctx, const AVCodec* decoder);

// Test-only: forces the next try_attach_hwaccel() call (and any
// already-cached process-wide device context) to behave as if hardware
// device creation failed. Not thread-safe — call only from single-threaded
// test setup, before constructing any VideoDecodeWorker that should observe
// the change. Pass false to restore normal (real) probing.
void test_only_force_hwaccel_unavailable(bool force);

// If `frame` is decoded in this platform's hw pixel format (only possible
// when try_attach_hwaccel() returned true for the codec context that
// produced it), transfers its data into `sw_frame` (allocated by the
// caller, reused across calls) via av_hwframe_transfer_data() — so
// `sw_frame` holds real pixel data in a software format the existing
// swscale-based FrameConverter pipeline can process — and returns true.
// Returns false (sw_frame left untouched) if `frame` is already in a
// software format, including every frame when no OSV_HWACCEL_* macro is
// compiled in. `frame`'s pts/best_effort_timestamp are copied onto
// `sw_frame`, since av_hwframe_transfer_data() only copies pixel data, not
// frame properties.
[[nodiscard]] bool transfer_hw_frame(const AVFrame* frame, AVFrame* sw_frame);

// Returns true if `frame` is still in this platform's hw pixel format —
// i.e. its data[] planes are an opaque device handle, not real pixel data.
// Only possible when an OSV_HWACCEL_* macro is compiled in and the codec
// context that produced `frame` had a hw_device_ctx attached; always false
// otherwise. Lets a caller distinguish transfer_hw_frame() returning false
// because there was legitimately nothing to transfer (frame already a
// software format) from it returning false because a genuine hw transfer
// failed (frame is still device-handle bytes, unusable as pixel data).
[[nodiscard]] bool is_hw_format_frame(const AVFrame* frame);

// Test-only: overrides is_hw_format_frame()'s return value regardless of
// build or the frame passed in, so tests can exercise the "hw transfer
// genuinely failed" path deterministically without a real hw device
// context. Pass std::nullopt to clear the override and restore real
// (build-dependent) behavior.
void test_only_force_is_hw_format_frame(std::optional<bool> force);

} // namespace media

#endif // OSV_VENDORED_AV
