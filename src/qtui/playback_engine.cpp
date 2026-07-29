#include "playback_engine.h"

#include <QtCore/qmetaobject.h>
#include <QtCore/qloggingcategory.h>
#include <QtCore/qthread.h>

#include "vault/vault.h"
#include "vault/index.h"
#include "media/video_source.h"
#include "media/chunk_avio.h"
#include "media/video_decoder.h"
#include "media/video_decode_worker.h"
#include "ui/playback_model.h"

Q_LOGGING_CATEGORY(lcPlayback, "osv.playback")

PlaybackEngine::PlaybackEngine(QObject* parent)
    : QObject(parent)
{
}

PlaybackEngine::~PlaybackEngine()
{
    // Stop the worker thread and clean up resources.
    // Order: stop thread → destroy worker/decoder/source
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }

    // Explicit cleanup (thread_ dtor would also clean these, but be explicit)
    worker_.reset();
    decoder_.reset();
    source_.reset();
}

void PlaybackEngine::open(quintptr nodeKey)
{
    // Called from GUI thread; resolve node and start playback
    if (!vault_ || !vault_->is_unlocked()) {
        qCWarning(lcPlayback) << "Vault not unlocked";
        return;
    }

    // Stop any existing playback
    stop();

    // Resolve the node
    const auto* node = reinterpret_cast<const vault::IndexNode*>(nodeKey);
    if (!node || node->type != vault::IndexNode::Type::Video) {
        qCWarning(lcPlayback) << "Invalid or non-video node";
        return;
    }

    try {
        // Open VideoSource
        media::VideoSource source = media::VideoSource::open(*vault_, *node);

        // Wrap in ChunkAvio to provide AVIOContext for FFmpeg
        avio_ = std::make_unique<media::ChunkAvio>(std::move(source));
        if (!avio_->valid()) {
            qCWarning(lcPlayback) << "Failed to create ChunkAvio";
            avio_.reset();
            return;
        }

        // Open VideoDecoder with the AVIOContext
        decoder_ = std::make_unique<media::VideoDecoder>();
        if (!decoder_->open(avio_->ctx())) {
            qCWarning(lcPlayback) << "Failed to open decoder";
            avio_.reset();
            decoder_.reset();
            return;
        }

        // Extract timing info
        duration_ = decoder_->duration_us() / 1e6;
        emit durationChanged();

        // Create decode worker
        auto params = decoder_->video_codecpar();
        auto time_base = decoder_->video_time_base();
        worker_ = std::make_unique<media::VideoDecodeWorker>(*params, time_base, 0);

        // Start the worker thread
        generation_ = 0;
        sentEof_ = false;
        clockBase_ = 0.0;
        elapsed_.restart();
        position_ = 0.0;

        thread_ = std::jthread([this](std::stop_token st) { runWorker(); });

        qDebug(lcPlayback) << "Video opened:" << duration_ << "seconds";
    } catch (const std::exception& e) {
        qCWarning(lcPlayback) << "Exception opening video:" << e.what();
        worker_.reset();
        decoder_.reset();
        source_.reset();
    }
}

void PlaybackEngine::stop()
{
    // Pause playback
    setPlaying(false);

    // Signal worker to stop
    if (thread_.joinable()) {
        thread_.request_stop();
        // Give worker a moment to see the stop request
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        thread_.join();
    }

    // Clean up in order: worker first (owns codec fed by pkts), then decoder, then avio (which owns source)
    worker_.reset();
    decoder_.reset();
    avio_.reset();

    position_ = 0.0;
    duration_ = 0.0;
    emit positionChanged();
    emit durationChanged();
}

void PlaybackEngine::setPlaying(bool on)
{
    if (playing_ == on) return;
    playing_ = on;

    if (on) {
        elapsed_.restart();
        clockBase_ = position_;
    }

    emit playingChanged();
}

void PlaybackEngine::seekBy(double s)
{
    // Clamp to [0, duration]
    double newPos = ui::clamp_time(position_ + s, duration_);
    position_ = newPos;

    // Signal the worker to seek
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingControl_ = ControlMsg{ControlMsg::Type::Seek, newPos, false};
    }

    emit positionChanged();
    emit clockTextChanged();
}

QString PlaybackEngine::clockText() const
{
    return QString::fromStdString(ui::format_clock(position_));
}

void PlaybackEngine::runWorker()
{
    std::stop_token st = std::this_thread::get_stop_token();

    while (!st.stop_requested()) {
        // Drain control queue
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pendingControl_) {
                const auto& msg = *pendingControl_;
                if (msg.type == ControlMsg::Type::Seek) {
                    ++generation_;
                    decoder_->seek_demux_only(msg.seekTarget);
                    worker_->begin_seek(msg.seekTarget);
                    clockBase_ = msg.seekTarget;
                    sentEof_ = false;
                } else if (msg.type == ControlMsg::Type::Stop) {
                    break;
                }
                pendingControl_.reset();
            }
        }

        // Check if we should fetch more packets
        if (!st.stop_requested() && worker_->outstanding() < 3 && !sentEof_) {
            auto* pkt = decoder_->demux_next_video_packet();
            worker_->submit(pkt, generation_);
            if (!pkt) {
                sentEof_ = true;
            }
        }

        // Check for decoded frames
        if (auto result = worker_->wait_result()) {
            // Discard mismatched generations (from old seeks)
            if (result->generation != generation_) {
                continue;
            }

            if (result->eof) {
                // Pause at end
                QMetaObject::invokeMethod(this, [this]() { setPlaying(false); }, Qt::QueuedConnection);
                continue;
            }

            if (result->frame) {
                // Compute current playback clock
                double clock = clockBase_ + elapsed_.elapsed() / 1000.0;

                // Check if frame is due
                if (ui::PlaybackModel::frame_due(clock, result->frame->pts_seconds)) {
                    // Frame is due now
                    auto frameBox = std::make_shared<FrameBox>();
                    frameBox->meta = *result->frame;
                    frameBox->storage = std::move(result->storage);

                    // Re-point the plane pointers into storage
                    frameBox->meta.planes[0] = frameBox->storage.data();
                    int y_size = frameBox->meta.linesizes[0] * frameBox->meta.height;
                    frameBox->meta.planes[1] = frameBox->storage.data() + y_size;
                    int u_size = frameBox->meta.linesizes[1] * ((frameBox->meta.height + 1) / 2);
                    frameBox->meta.planes[2] = frameBox->storage.data() + y_size + u_size;

                    // Update position and deliver frame
                    double newPos = result->frame->pts_seconds;
                    QMetaObject::invokeMethod(this, [this, frameBox, newPos]() {
                        position_ = newPos;
                        emit positionChanged();
                        onFrameReady(frameBox);
                    }, Qt::QueuedConnection);
                } else {
                    // Frame not due yet; sleep for the delta
                    double delay = result->frame->pts_seconds - clock;
                    if (delay > 0.001) {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(static_cast<int>(delay * 1000)));
                    }
                }
            }
        }

        // Yield to prevent spinning
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void PlaybackEngine::onFrameReady(std::shared_ptr<const FrameBox> frame)
{
    if (frameItem_) {
        frameItem_->setFrame(frame);
    }
    emit clockTextChanged();
}
