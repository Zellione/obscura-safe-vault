#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QPointer>
#include <memory>
#include <atomic>
#include <thread>
#include <stop_token>
#include <mutex>
#include <optional>
#include <cstdint>

#include "video_frame_item.h"
#include "audio_pipe.h"

// Forward declarations
class VideoFrameItem;
class AudioPipe;
namespace vault { class Vault; class IndexNode; }
namespace media {
    class VideoSource;
    class ChunkAvio;
    class VideoDecoder;
    class VideoDecodeWorker;
}

// PlaybackEngine: orchestrates video + audio playback (M6a video + M6b audio+sync).
// Manages demuxing, decoding, frame pacing, audio feeding, and lifetime coordination.
//
// Threading:
// - GUI thread: open(nodeKey), play(), pause(), seekBy(), stop(), setVolume(), mute/unmute
// - Worker thread (jthread): demux video+audio packets, submit to decoders, pace frames via av_sync clock
// - Frame delivery: queued invoke to setFrame() on GUI thread
// - Audio clock: computed from audio samples_consumed, driving frame presentation timing
class PlaybackEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(QString clockText READ clockText NOTIFY clockTextChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)

public:
    explicit PlaybackEngine(QObject* parent = nullptr);
    ~PlaybackEngine();

    PlaybackEngine(const PlaybackEngine&) = delete;
    PlaybackEngine& operator=(const PlaybackEngine&) = delete;

    // Bind the renderer for frame delivery
    Q_INVOKABLE void setFrameItem(VideoFrameItem* item) { frameItem_ = item; }

    // Bind the vault for node resolution
    void setVault(vault::Vault* vault) { vault_ = vault; }

    // Public interface (all callable from GUI thread)
    Q_INVOKABLE void open(quintptr nodeKey);
    Q_INVOKABLE void play() { setPlaying(true); }
    Q_INVOKABLE void pause() { setPlaying(false); }
    Q_INVOKABLE void seekBy(double s);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setVolume(double v);
    Q_INVOKABLE void toggleMute();

    [[nodiscard]] double position() const { return position_; }
    [[nodiscard]] double duration() const { return duration_; }
    [[nodiscard]] bool playing() const { return playing_; }
    [[nodiscard]] QString clockText() const;
    [[nodiscard]] bool muted() const { return muted_; }
    [[nodiscard]] double volume() const { return volume_; }

    // Test seams (M6b audio testing)
    [[nodiscard]] uint64_t testOnlySamplesConsumed() const;
    [[nodiscard]] bool testOnlyAudioFallback() const { return audioUsingFallback_; }
    [[nodiscard]] float testOnlyCurrentGain() const { return currentGain_; }

    void setMuted(bool m);

signals:
    void positionChanged();
    void durationChanged();
    void playingChanged();
    void clockTextChanged();
    void mutedChanged();
    void volumeChanged();

private:
    void setPlaying(bool on);

    struct ControlMsg {
        enum class Type { Seek, Stop, PlayPause };
        Type type;
        double seekTarget = 0.0;
        bool play = false;
    };

    // Worker thread main loop (receives stop_token from jthread)
    void runWorker(std::stop_token st);

    // GUI thread: called by runWorker via queued invoke to render a frame
    void onFrameReady(std::shared_ptr<const FrameBox> frame);

    // --- GUI thread state ---
    QPointer<VideoFrameItem> frameItem_;
    vault::Vault* vault_ = nullptr;
    double position_ = 0.0;
    double duration_ = 0.0;
    bool playing_ = false;
    bool muted_ = false;
    double volume_ = 1.0;
    float currentGain_ = 1.0f;  // Last gain applied to audio pipe (for testing)

    // --- Shared state (guarded by mutex) ---
    mutable std::mutex mutex_;
    std::optional<ControlMsg> pendingControl_;
    double audioSeekBase_ = 0.0;      // set on seek; audio clock = base + consumed/rate
    bool audioUsingFallback_ = false; // audio exists but dummy driver doesn't consume → use wall clock

    // --- Worker thread state ---
    std::unique_ptr<media::ChunkAvio> avio_;        // owns the VideoSource internally
    std::unique_ptr<media::VideoDecoder> decoder_;
    std::unique_ptr<media::VideoDecodeWorker> worker_;
    std::unique_ptr<AudioPipe> audioPipe_;          // manages SDL audio stream (M6b)
    uint64_t generation_ = 0;
    QElapsedTimer elapsed_;  // clock base for frame pacing (M6a fallback)
    double clockBase_ = 0.0;  // seek/rewind base PTS
    bool sentEof_ = false;
    std::jthread thread_;
};
