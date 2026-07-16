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
                                     uint32_t wake_event)
    : time_base_(time_base), wake_event_(wake_event)
{
    const AVCodec* decoder = avcodec_find_decoder(params.codec_id);
    if (decoder) {
        codec_ctx_ = avcodec_alloc_context3(decoder);
        if (codec_ctx_) {
            if (avcodec_parameters_to_context(codec_ctx_, &params) < 0 ||
                avcodec_open2(codec_ctx_, decoder, nullptr) < 0) {
                avcodec_free_context(&codec_ctx_);
            }
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
    std::unique_lock lock(mtx_);
    cv_done_.wait_for(lock, std::chrono::milliseconds(500),
                      [this] { return stop_ || !done_.empty(); });
    if (done_.empty()) return std::nullopt;
    Result r = std::move(done_.front());
    done_.erase(done_.begin());
    return r;
}

void VideoDecodeWorker::run()
{
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mtx_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty()) return;
            job = queue_.front();
            queue_.pop_front();
        }

        if (!codec_ctx_) {
            if (job.pkt) av_packet_free(&job.pkt);
            continue;
        }

        if (job.pkt) {
            avcodec_send_packet(codec_ctx_, job.pkt);
            av_packet_free(&job.pkt);
            flushed_ = false;
        } else if (!flushed_) {
            avcodec_send_packet(codec_ctx_, nullptr);   // flush
            flushed_ = true;
        }

        for (;;) {
            const int ret = avcodec_receive_frame(codec_ctx_, frame_);
            if (ret == 0) {
                double pts_seconds = 0.0;
                if (frame_->best_effort_timestamp != AV_NOPTS_VALUE)
                    pts_seconds = static_cast<double>(frame_->best_effort_timestamp) * time_base_;

                std::optional<DecodedFrame> decoded =
                    (frame_->format == AV_PIX_FMT_YUV420P || frame_->format == AV_PIX_FMT_NV12)
                        ? std::optional<DecodedFrame>(FrameConverter::zero_copy(frame_, pts_seconds))
                        : conv_.to_i420(frame_, pts_seconds);
                if (!decoded) continue;   // conversion failed; skip this frame

                if (pending_seek_target_ >= 0.0 && decoded->pts_seconds < pending_seek_target_)
                    continue;   // decode-forward past frames before a pending seek target
                pending_seek_target_ = -1.0;

                Result r;
                r.generation = job.generation;
                std::vector<uint8_t> storage;
                r.frame   = copy_owned_frame(*decoded, storage);
                r.storage = std::move(storage);
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
                if (job.pkt == nullptr) {
                    // Keep draining after flush; don't break yet
                    continue;
                }
                break;   // one frame per submitted job, matching decode_from_current_frame() cadence
            }
            if (ret == AVERROR(EAGAIN)) {
                if (job.pkt == nullptr) {
                    // EOF drain complete: all buffered frames exhausted
                    Result r;
                    r.generation = job.generation;
                    r.eof = true;
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
                break;   // need another packet; back to outer loop
            }
            // Any other error; if flushing, publish EOF
            if (job.pkt == nullptr) {
                Result r;
                r.generation = job.generation;
                r.eof = true;
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
            break;
        }
    }
}

} // namespace media

#endif // OSV_VENDORED_AV
