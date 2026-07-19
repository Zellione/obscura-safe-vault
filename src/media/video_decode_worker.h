#pragma once

#ifdef OSV_VENDORED_AV

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "media/decoded_frame.h"
#include "media/frame_convert.h"
#include "media/hw_accel.h"

struct AVCodecContext;
struct AVCodecParameters;
struct AVFrame;
struct AVPacket;

namespace media {

// Off-thread video codec decode so playback never stalls the render thread
// on a slow codec (e.g. software AV1/HEVC). Owns an independent
// AVCodecContext opened from the caller's AVCodecParameters — completely
// separate from any VideoDecoder's own decode state, so there is no shared
// mutable state between this worker's thread and the render thread.
//
// This worker does no demuxing and touches no AVIOContext/vault I/O: the
// caller (render thread) demuxes via VideoDecoder::demux_next_video_packet()
// and hands packets to submit(). See docs/superpowers/specs/2026-07-16-async-video-decode-design.md
// for the full design rationale.
class VideoDecodeWorker {
public:
    struct Result {
        uint64_t generation = 0;
        bool     eof        = false;   // true: end of stream for `generation`; frame unset
        std::optional<DecodedFrame> frame;
        std::vector<uint8_t>        storage;   // owns frame's plane bytes
    };

    // `wake_event` is an SDL event type (e.g. image::decode_wake_event()) pushed
    // whenever a result is published, so an event-driven loop wakes up to drain
    // it. Pass 0 to disable the wake (e.g. in headless tests).
    //
    // `test_only_decode_delay` sleeps this amount before each job's decode
    // when nonzero; production callers always leave it at the default
    // (zero — no behavior change). It exists only so tests can deterministically
    // simulate a codec slower than VideoDecodeWorker::wait_result()'s timeout,
    // without depending on real-world decode timing.
    VideoDecodeWorker(const AVCodecParameters& params, double time_base, uint32_t wake_event,
                       std::chrono::milliseconds test_only_decode_delay = std::chrono::milliseconds(0));
    ~VideoDecodeWorker();

    VideoDecodeWorker(const VideoDecodeWorker&)            = delete;
    VideoDecodeWorker& operator=(const VideoDecodeWorker&) = delete;

    // Queue a demuxed video-stream packet (ownership transferred; freed
    // internally once decoded) for decode under `generation`. Pass
    // pkt == nullptr once, after the last packet of a generation, to signal
    // end-of-stream so the worker flushes and drains any buffered frames.
    void submit(AVPacket* pkt, uint64_t generation);

    // Called when a seek supersedes whatever the worker was doing: drops
    // every not-yet-decoded queued packet (frees them) and sets the target
    // PTS frames below which are discarded internally (not published) once
    // decoding resumes — mirrors VideoDecoder::seek()'s
    // decode-forward-to-target behavior, moved to this worker's own codec
    // context. Doesn't need a generation argument: each subsequent submit()
    // already stamps its own Job with whatever generation the caller is on,
    // and that per-Job value (not anything begin_seek() would try to record)
    // is what ends up on the published Result. Jobs already mid-decode when
    // this is called finish normally, carrying their OLD generation; callers
    // must compare Result::generation against their own current value and
    // discard mismatches themselves (this class does not filter).
    void begin_seek(double target_pts);

    // Pop one finished result, or nullopt if none ready. Non-blocking.
    [[nodiscard]] std::optional<Result> take_result();

    // Blocks (bounded by a short timeout, so a stuck decode can never hang
    // the caller forever) until a result is published or the worker stops.
    // Returns nullopt on timeout or if the worker has nothing pending and is
    // stopping. The render thread uses this — not take_result() — as its
    // main way to get the next frame: it prefetches a couple of packets
    // ahead (see VideoPlayback::Impl::decode_into_pending(), Task 5) so
    // that by the time it actually needs to block here, the worker already
    // has a head start decoding them in parallel with whatever the render
    // thread was just doing (GPU upload, present, event handling). The
    // timeout is short (tens of ms), not a long poll interval: while a
    // seek target is pending, the worker can silently skip packets whose
    // decoded pts lands before the target — those never publish a Result
    // — so the caller can't distinguish "still decoding" from "needs more
    // input" except by retrying with fresh input on timeout, in a loop
    // (this does not mean a slow real decode gets abandoned; the caller
    // just loops back and waits again for the same in-flight work).
    [[nodiscard]] std::optional<Result> wait_result();

private:
    struct Job {
        AVPacket* pkt        = nullptr;   // nullptr = end-of-stream marker
        uint64_t  generation = 0;
    };

    void run();
    std::optional<Job> wait_for_job();
    void send_packet(Job& job);
    void decode_available_frames(const Job& job, bool is_flush);
    bool publish_decoded_frame(const Job& job);
    void publish_result(Result&& r);
    void publish_eof(uint64_t generation);
    bool reopen_software_only();

    // Consumes (clears) a pending post-seek flush scheduled by begin_seek(),
    // returning whether one was pending. Must only be called from run()
    // (this worker's own thread) — avcodec_flush_buffers() is not safe to
    // call concurrently with a decode in progress on the same context, which
    // is why begin_seek() (called from the render thread) can't just call it
    // directly and instead defers to here via this flag.
    bool consume_pending_flush();

    // Test-only seams (defined in tests/media/test_video_decode_worker.cpp,
    // not part of any production translation unit) — exercise the
    // hw-failure recovery path deterministically without real hardware:
    // production code can never observe a real hw decode failure on a CI
    // runner with no GPU decode block.
    friend bool test_only_reopen_software(VideoDecodeWorker& w);
    friend void test_only_force_hw_active(VideoDecodeWorker& w, bool active);
    friend bool test_only_pending_flush(VideoDecodeWorker& w);

    AVCodecContext*        codec_ctx_ = nullptr;
    AVFrame*               frame_     = nullptr;
    FrameConverter         conv_;
    double                 time_base_ = 0.0;
    std::atomic<double>    pending_seek_target_{-1.0};
    bool                   flushed_ = false;
    bool                   pending_flush_ = false;   // guarded by mtx_; set by begin_seek(),
                                                       // consumed by run() on the worker thread
    bool                       hw_active_        = false;
    AVCodecParameters*         saved_params_     = nullptr;
    AVFrame*                   hw_transfer_frame_ = nullptr;   // lazily allocated; reused across frames
    std::chrono::milliseconds test_only_decode_delay_{0};

    mutable std::mutex      mtx_;
    std::condition_variable cv_;         // signals run(): queue_ gained a Job, or stop_ was set
    std::condition_variable cv_done_;    // signals wait_result(): done_ gained a Result, or stop_ was set
    std::deque<Job>         queue_;
    std::vector<Result>     done_;
    bool                    stop_ = false;
    uint32_t                wake_event_ = 0;
    std::jthread            thread_;
};

} // namespace media

#endif // OSV_VENDORED_AV
