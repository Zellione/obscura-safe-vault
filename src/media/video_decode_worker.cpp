#ifdef OSV_VENDORED_AV

#include "media/video_decode_worker.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
extern "C" {
#include <libavcodec/avcodec.h>
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <chrono>
#include <print>
#include <utility>

#include "media/frame_convert.h"
#include "media/hw_accel.h"

namespace media {

VideoDecodeWorker::VideoDecodeWorker(const AVCodecParameters& params, double time_base,
                                     uint32_t wake_event,
                                     std::chrono::milliseconds test_only_decode_delay)
    : time_base_(time_base), test_only_decode_delay_(test_only_decode_delay),
      wake_event_(wake_event)
{
    // Own a copy of the caller's codec parameters: `params` is only valid
    // for this call, but reopen_software_only() needs them again if a hw
    // context fails mid-clip.
    saved_params_ = avcodec_parameters_alloc();
    if (saved_params_) avcodec_parameters_copy(saved_params_, &params);

    if (const AVCodec* decoder = avcodec_find_decoder(params.codec_id); decoder) {
        codec_ctx_ = avcodec_alloc_context3(decoder);
        if (codec_ctx_ && avcodec_parameters_to_context(codec_ctx_, &params) >= 0) {
            hw_active_ = try_attach_hwaccel(codec_ctx_, decoder);
            if (avcodec_open2(codec_ctx_, decoder, nullptr) < 0)
                avcodec_free_context(&codec_ctx_);
        } else if (codec_ctx_) {
            avcodec_free_context(&codec_ctx_);
        }
    }
    if (!codec_ctx_) std::println(stderr, "[VideoDecodeWorker] failed to open codec context");
    frame_ = av_frame_alloc();
    thread_ = std::jthread([this] { run(); });
}

VideoDecodeWorker::~VideoDecodeWorker()
{
    {
        std::lock_guard lock(mtx_);
        stop_ = true;
    }
    cv_.notify_all();
    cv_done_.notify_all();
    if (thread_.joinable()) thread_.join();

    for (auto& job : queue_) if (job.pkt) av_packet_free(&job.pkt);
    if (frame_)             av_frame_free(&frame_);
    if (hw_transfer_frame_) av_frame_free(&hw_transfer_frame_);
    if (codec_ctx_)         avcodec_free_context(&codec_ctx_);
    if (saved_params_)      avcodec_parameters_free(&saved_params_);
}

void VideoDecodeWorker::submit(AVPacket* pkt, uint64_t generation)
{
    {
        std::lock_guard lock(mtx_);
        queue_.push_back(Job{pkt, generation});
    }
    cv_.notify_one();
}

void VideoDecodeWorker::begin_seek(double target_pts)
{
    std::lock_guard lock(mtx_);
    for (auto& job : queue_) if (job.pkt) av_packet_free(&job.pkt);
    queue_.clear();
    pending_seek_target_ = target_pts;
    pending_flush_ = true;
}

bool VideoDecodeWorker::consume_pending_flush()
{
    std::lock_guard lock(mtx_);
    const bool was = pending_flush_;
    pending_flush_ = false;
    return was;
}

std::optional<VideoDecodeWorker::Result> VideoDecodeWorker::take_result()
{
    std::lock_guard lock(mtx_);
    if (done_.empty()) return std::nullopt;
    Result r = std::move(done_.front());
    done_.erase(done_.begin());
    return r;
}

std::optional<VideoDecodeWorker::Result> VideoDecodeWorker::wait_result()
{
    // Short timeout, not a long poll interval: the caller (VideoPlayback::
    // Impl::decode_into_pending()) retries in a loop, feeding one more
    // packet on each timeout when the worker might be silently skipping
    // packets toward a pending seek target (those never publish a Result,
    // so the caller can't tell "still decoding" from "needs more input"
    // apart except by trying). A short timeout keeps that retry cadence
    // fast; it does not mean a slow real decode gets abandoned — the
    // caller just loops back and waits again for the same in-flight work.
    std::unique_lock lock(mtx_);
    cv_done_.wait_for(lock, std::chrono::milliseconds(20),
                      [this] { return stop_ || !done_.empty(); });
    if (done_.empty()) return std::nullopt;
    Result r = std::move(done_.front());
    done_.erase(done_.begin());
    return r;
}

std::optional<VideoDecodeWorker::Job> VideoDecodeWorker::wait_for_job()
{
    std::unique_lock lock(mtx_);
    cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
    // Bail out on stop_ regardless of queue_ — draining every
    // still-queued (not-yet-started) job before honoring stop_
    // would decode a whole backlog nobody will ever consume
    // (the destructor is already discarding these results by
    // tearing this object down). With a real slow codec, up to
    // MAX_STEADY_IN_FLIGHT worth of packets can be queued at
    // once, so this used to make destruction block for seconds —
    // observed as the app freezing whenever playback was torn
    // down (e.g. navigating away right as a clip ends) shortly
    // after a stretch of slow decode. The destructor's own
    // cleanup loop frees whatever's left in queue_ after this
    // returns.
    if (stop_) return std::nullopt;
    Job job = queue_.front();
    queue_.pop_front();
    return job;
}

void VideoDecodeWorker::publish_result(Result&& r)
{
    {
        std::lock_guard lock(mtx_);
        done_.push_back(std::move(r));
    }
    cv_done_.notify_one();
    if (wake_event_ != 0) {
        SDL_Event e{};
        e.type = wake_event_;
        SDL_PushEvent(&e);
    }
}

void VideoDecodeWorker::publish_eof(uint64_t generation)
{
    Result r;
    r.generation = generation;
    r.eof = true;
    publish_result(std::move(r));
}

void VideoDecodeWorker::send_packet(Job& job)
{
    if (job.pkt) {
        avcodec_send_packet(codec_ctx_, job.pkt);
        av_packet_free(&job.pkt);
        flushed_ = false;
    } else if (!flushed_) {
        avcodec_send_packet(codec_ctx_, nullptr);  // flush
        flushed_ = true;
    }
}

// Converts and publishes the frame just received into frame_. Returns false
// if the frame was skipped — conversion failed, or its pts lands before a
// pending seek target — rather than published.
bool VideoDecodeWorker::publish_decoded_frame(const Job& job)
{
    // A hw-decoded frame's data[] planes are an opaque device handle, not
    // real pixel data — transfer to system memory first so the existing
    // zero-copy/swscale pipeline below has something it can actually read.
    const AVFrame* src = frame_;
    if (hw_active_) {
        if (!hw_transfer_frame_) hw_transfer_frame_ = av_frame_alloc();
        if (hw_transfer_frame_ && transfer_hw_frame(frame_, hw_transfer_frame_)) {
            src = hw_transfer_frame_;
        } else if (is_hw_format_frame(frame_)) {
            // frame_ is still an opaque hw device handle — transfer_hw_frame()
            // genuinely failed rather than having nothing to do (that case
            // would mean frame_ is already a software format, safe to use
            // as-is below). Feeding device-handle bytes to the swscale-based
            // converter as if they were pixel planes is undefined behavior;
            // skip this frame instead.
            return false;
        }
    }

    double pts_seconds = 0.0;
    if (src->best_effort_timestamp != AV_NOPTS_VALUE)
        pts_seconds = static_cast<double>(src->best_effort_timestamp) * time_base_;

    std::optional<DecodedFrame> decoded =
        (src->format == AV_PIX_FMT_YUV420P || src->format == AV_PIX_FMT_NV12)
            ? std::optional<DecodedFrame>(FrameConverter::zero_copy(src, pts_seconds))
            : conv_.to_i420(src, pts_seconds);
    if (!decoded) return false;  // conversion failed; skip this frame

    if (double seek_target = pending_seek_target_.load();
        seek_target >= 0.0 && decoded->pts_seconds < seek_target)
        return false;  // decode-forward past frames before a pending seek target
    pending_seek_target_.store(-1.0);

    Result r;
    r.generation = job.generation;
    std::vector<uint8_t> storage;
    r.frame = copy_owned_frame(*decoded, storage);
    r.storage = std::move(storage);
    publish_result(std::move(r));
    return true;
}

void VideoDecodeWorker::decode_available_frames(const Job& job, bool is_flush)
{
    for (;;) {
        const int ret = avcodec_receive_frame(codec_ctx_, frame_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            if (is_flush) publish_eof(job.generation);
            return;
        }
        if (ret < 0) {
            // A hard decode error. If a hw context was active, this is
            // exactly the "GPU driver/hardware couldn't handle this
            // stream" case the opportunistic-hwaccel invariant requires
            // recovering from: drop the failed context and resume the
            // rest of this clip in software. This job's frame is lost
            // (one dropped frame during a rare hw->sw transition, versus
            // aborting playback) — reopen_software_only() leaves
            // codec_ctx_ ready for the *next* job's packet. Regardless of
            // whether recovery succeeds, if this was the end-of-stream
            // flush job, its eof must still be published — silently
            // returning here would leave the caller's eof flag stuck
            // false forever, since nothing else is left to feed or wait
            // on for this generation.
            if (hw_active_) reopen_software_only();
            if (is_flush) publish_eof(job.generation);
            return;
        }
        if (!publish_decoded_frame(job)) continue;  // skipped; try the next buffered frame
        if (is_flush) continue;                     // keep draining after flush; don't return yet
        return;  // one frame per submitted job, matching decode_from_current_frame() cadence
    }
}

// Drops the current (hw-attached) codec context and opens a fresh
// software-only one from the caller's original AVCodecParameters (saved at
// construction, since `params` in the constructor is only a reference valid
// for that call). Returns false if the codec can no longer be opened at all
// (extremely unlikely — the same decoder just opened successfully moments
// ago) — the caller treats codec_ctx_ == nullptr the same as construction
// failure (run() drops jobs without decoding, per wait_for_job()'s existing
// contract).
bool VideoDecodeWorker::reopen_software_only()
{
    if (codec_ctx_) avcodec_free_context(&codec_ctx_);
    const AVCodec* decoder = saved_params_ ? avcodec_find_decoder(saved_params_->codec_id) : nullptr;
    if (!decoder) return false;
    codec_ctx_ = avcodec_alloc_context3(decoder);
    if (!codec_ctx_) return false;
    if (avcodec_parameters_to_context(codec_ctx_, saved_params_) < 0 ||
        avcodec_open2(codec_ctx_, decoder, nullptr) < 0) {
        avcodec_free_context(&codec_ctx_);
        return false;
    }
    hw_active_ = false;
    flushed_   = false;
    return true;
}

void VideoDecodeWorker::run()
{
    for (;;) {
        std::optional<Job> job = wait_for_job();
        if (!job) return;

        if (!codec_ctx_) {
            if (job->pkt) av_packet_free(&job->pkt);
            continue;
        }

        if (consume_pending_flush()) {
            // A seek superseded whatever this context was doing: discard
            // buffered reference/reorder-queue state before decoding
            // anything from the new position. Without this, packets
            // demuxed from the new seek target get fed into a decoder
            // still holding pre-seek B-frame reorder/reference state —
            // software decoders are mostly forgiving of this, but a
            // hardware decoder's fixed-size surface pool is not: stale
            // surface references left over from before the seek can
            // starve the pool and stall decode indefinitely. Mirrors
            // VideoDecoder::seek()'s own avcodec_flush_buffers() call for
            // the non-async path (video_decoder.cpp).
            avcodec_flush_buffers(codec_ctx_);
            flushed_ = false;
        }

        if (test_only_decode_delay_.count() > 0)
            std::this_thread::sleep_for(test_only_decode_delay_);

        // Capture before send_packet() nulls job->pkt as a side effect —
        // every later "is this the flush marker?" check must use is_flush,
        // never job->pkt itself, or a real packet that happens to return
        // EAGAIN on its first avcodec_receive_frame() (routine — many
        // codecs need more than one packet before yielding a frame) would
        // be indistinguishable from an actual end-of-stream flush.
        const bool is_flush = (job->pkt == nullptr);
        send_packet(*job);
        decode_available_frames(*job, is_flush);
    }
}

} // namespace media

#endif // OSV_VENDORED_AV
