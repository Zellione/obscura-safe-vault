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

namespace media {

VideoDecodeWorker::VideoDecodeWorker(const AVCodecParameters& params, double time_base,
                                     uint32_t wake_event,
                                     std::chrono::milliseconds test_only_decode_delay)
    : time_base_(time_base), test_only_decode_delay_(test_only_decode_delay),
      wake_event_(wake_event)
{
    if (const AVCodec* decoder = avcodec_find_decoder(params.codec_id); decoder) {
        codec_ctx_ = avcodec_alloc_context3(decoder);
        if (codec_ctx_ && (avcodec_parameters_to_context(codec_ctx_, &params) < 0 ||
                           avcodec_open2(codec_ctx_, decoder, nullptr) < 0)) {
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
    if (frame_)     av_frame_free(&frame_);
    if (codec_ctx_) avcodec_free_context(&codec_ctx_);
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
    double pts_seconds = 0.0;
    if (frame_->best_effort_timestamp != AV_NOPTS_VALUE)
        pts_seconds = static_cast<double>(frame_->best_effort_timestamp) * time_base_;

    std::optional<DecodedFrame> decoded =
        (frame_->format == AV_PIX_FMT_YUV420P || frame_->format == AV_PIX_FMT_NV12)
            ? std::optional<DecodedFrame>(FrameConverter::zero_copy(frame_, pts_seconds))
            : conv_.to_i420(frame_, pts_seconds);
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
        if (const int ret = avcodec_receive_frame(codec_ctx_, frame_); ret != 0) {
            // Whether this is EAGAIN (need another packet) or a real
            // decode error, the drain is over: if flushing, that's the
            // signal all buffered frames are exhausted, so publish EOF.
            if (is_flush) publish_eof(job.generation);
            return;
        }
        if (!publish_decoded_frame(job)) continue;  // skipped; try the next buffered frame
        if (is_flush) continue;                     // keep draining after flush; don't return yet
        return;  // one frame per submitted job, matching decode_from_current_frame() cadence
    }
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
