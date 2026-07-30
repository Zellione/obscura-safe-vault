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
#include "media/av_sync.h"
#include "ui/playback_model.h"

Q_LOGGING_CATEGORY(lcPlayback, "osv.playback")

PlaybackEngine::PlaybackEngine(QObject* parent)
    : QObject(parent)
{
}

PlaybackEngine::~PlaybackEngine()
{
    // Stop the worker thread and clean up resources.
    // Order: stop thread → destroy worker/decoder/avio (which owns source)
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }

    // Explicit cleanup (thread_ dtor would also clean these, but be explicit)
    worker_.reset();
    decoder_.reset();
    avio_.reset();
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
        // Open VideoSource and wrap in ChunkAvio (ChunkAvio takes ownership)
        media::VideoSource source = media::VideoSource::open(*vault_, *node);
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

        // Audio initialization deferred to worker thread (M6b, avoids blocking GUI on device setup)

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

        thread_ = std::jthread([this](std::stop_token st) { runWorker(st); });

        // Auto-start playback when a video is opened
        setPlaying(true);

        qCDebug(lcPlayback) << "Video opened:" << duration_ << "seconds";
    } catch (const std::exception& e) {
        qCWarning(lcPlayback) << "Exception opening video:" << e.what();
        worker_.reset();
        decoder_.reset();
        avio_.reset();  // avio_ owns source, so reset it
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

    // Clean up in order: audio pipe, then worker, decoder, avio
    // Audio pipe must be torn down before decoder (no pending callbacks)
    audioPipe_.reset();
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
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (playing_ == on) return;
        playing_ = on;
        changed = true;

        if (on) {
            elapsed_.restart();
            clockBase_ = position_;
        }
    }

    if (changed) {
        // Pause/resume audio stream
        if (audioPipe_) {
            if (on) {
                audioPipe_->resume();
            } else {
                audioPipe_->pause();
            }
        }
        emit playingChanged();
    }
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

void PlaybackEngine::setVolume(double v)
{
    double clamped = v;
    if (clamped < 0.0) clamped = 0.0;
    if (clamped > 1.0) clamped = 1.0;

    if (volume_ == clamped) return;

    volume_ = clamped;
    if (audioPipe_) {
        audioPipe_->set_gain(media::effective_gain(static_cast<float>(volume_), muted_));
    }
    emit volumeChanged();
}

void PlaybackEngine::setMuted(bool m)
{
    if (muted_ == m) return;
    muted_ = m;
    if (audioPipe_) {
        audioPipe_->set_gain(media::effective_gain(static_cast<float>(volume_), muted_));
    }
    emit mutedChanged();
}

void PlaybackEngine::toggleMute()
{
    setMuted(!muted_);
}

void PlaybackEngine::runWorker(std::stop_token st)
{
    bool dummy_driver_warned = false;
    bool audio_initialized = false;
    int audio_no_consume_count = 0;  // Track iterations where samples_consumed stays at 0
    uint64_t last_samples_consumed = 0;

    while (!st.stop_requested()) {
        // Lazy initialize audio on first worker iteration (avoids blocking GUI thread)
        if (!audio_initialized) {
            audio_initialized = true;  // Only try once
            if (decoder_ && decoder_->has_audio()) {
                audioPipe_ = std::make_unique<AudioPipe>();
                auto ainfo = decoder_->audio_info();
                if (audioPipe_->open(ainfo.channels, ainfo.sample_rate)) {
                    qCDebug(lcPlayback) << "Audio device opened successfully";
                } else {
                    qCWarning(lcPlayback) << "Failed to open audio device";
                    audioPipe_.reset();  // audio optional; keep playing video
                }
            }
        }

        // Drain control queue
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pendingControl_) {
                const auto& msg = *pendingControl_;
                if (msg.type == ControlMsg::Type::Seek) {
                    ++generation_;
                    if (!decoder_->seek_demux_only(msg.seekTarget)) {
                        qCWarning(lcPlayback) << "Seek failed at" << msg.seekTarget;
                    } else {
                        worker_->begin_seek(msg.seekTarget);
                        clockBase_ = msg.seekTarget;
                        audioSeekBase_ = msg.seekTarget;
                        sentEof_ = false;
                        // Clear audio stream on seek (reset fed/eof tracking)
                        if (audioPipe_) {
                            audioPipe_->clear();
                        }
                    }
                } else if (msg.type == ControlMsg::Type::Stop) {
                    break;
                }
                pendingControl_.reset();
            }
        }

        // Pump audio frames (M6b) - mirrors SDL app pump_audio (lines 366-379)
        // Feed until ~200ms queued, or decoder EOF reached
        if (audioPipe_) {
            audioPipe_->pump_audio(
                [this]() { return decoder_->next_audio_frame(); },
                decoder_->audio_info().sample_rate,
                decoder_->audio_info().channels
            );

            // Detect if audio device is not consuming (dummy driver or no hardware)
            uint64_t current_consumed = audioPipe_->samples_consumed();
            if (current_consumed == last_samples_consumed) {
                audio_no_consume_count++;
                if (audio_no_consume_count >= 10 && !dummy_driver_warned) {
                    // After 10 iterations with no consumed samples, switch to wall-clock
                    std::lock_guard<std::mutex> lock(mutex_);
                    audioUsingFallback_ = true;
                    dummy_driver_warned = true;
                    qCDebug(lcPlayback) << "Audio not consuming samples, switching to wall-clock fallback";
                }
            } else {
                // Audio is consuming, keep using audio clock
                audio_no_consume_count = 0;
                last_samples_consumed = current_consumed;
            }
        }

        // Check if we should fetch more packets
        bool shouldPlay = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shouldPlay = playing_;
        }
        if (!st.stop_requested() && shouldPlay && worker_->outstanding() < 3 && !sentEof_) {
            auto* pkt = decoder_->demux_next_video_packet();
            worker_->submit(pkt, generation_);
            if (!pkt) {
                sentEof_ = true;
            }
        }

        // Try to fetch and deliver a frame (short wait, don't block)
        auto result = worker_->wait_result();
        if (result) {
            // Discard mismatched generations (from old seeks)
            if (result->generation != generation_) {
                // Stale result; discard and continue
            } else if (result->eof) {
                // End of stream; pause playback
                QMetaObject::invokeMethod(this, [this]() { setPlaying(false); }, Qt::QueuedConnection);
            } else if (result->frame) {
                // Compute clock and decide on frame action
                double clock = 0.0;
                bool using_audio_clock = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (audioPipe_ && !audioUsingFallback_) {
                        // Use audio clock for synchronization (M6b audio-driven playback)
                        uint64_t samples_consumed = audioPipe_->samples_consumed();
                        clock = media::audio_clock(audioSeekBase_, samples_consumed,
                                                   decoder_->audio_info().sample_rate);
                        using_audio_clock = true;
                    } else {
                        // Wall clock fallback (M6a video-only, or audio unavailable)
                        clock = clockBase_ + elapsed_.elapsed() / 1000.0;
                    }
                }

                bool should_present = false;
                if (using_audio_clock) {
                    // With audio clock, use av_sync for precise frame pacing
                    media::FrameAction action = media::decide(clock, result->frame->pts_seconds);
                    should_present = (action == media::FrameAction::Present);
                } else {
                    // With wall clock, use simple delay-based pacing (original M6a approach)
                    double delay = result->frame->pts_seconds - clock;
                    if (delay > 0.001) {
                        // Frame not yet due; sleep until it is
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(static_cast<int>(delay * 1000)));
                        // Check for stop request, otherwise present the frame
                        if (st.stop_requested()) {
                            continue;
                        }
                    }
                    should_present = true;
                }

                if (should_present) {
                    // Build and deliver the frame
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
