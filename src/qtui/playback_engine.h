#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <optional>
#include <cstdint>

#include "video_frame_item.h"

// Forward declarations
class VideoFrameItem;
namespace vault { class Vault; class IndexNode; }
namespace media {
    class VideoSource;
    class ChunkAvio;
    class VideoDecoder;
    class VideoDecodeWorker;
}

// PlaybackEngine: orchestrates video-only playback (M6a, no audio yet).
// Manages demuxing, decoding, frame pacing, and lifetime coordination.
//
// Threading:
// - GUI thread: open(nodeKey), play(), pause(), seekBy(), stop()
// - Worker thread (jthread): demux, submit packets, pace frames via clock
// - Frame delivery: queued invoke to setFrame() on GUI thread
class PlaybackEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(QString clockText READ clockText NOTIFY clockTextChanged)

public:
    explicit PlaybackEngine(QObject* parent = nullptr);
    ~PlaybackEngine();

    PlaybackEngine(const PlaybackEngine&) = delete;
    PlaybackEngine& operator=(const PlaybackEngine&) = delete;

    // Bind the renderer for frame delivery
    void setFrameItem(VideoFrameItem* item) { frameItem_ = item; }

    // Bind the vault for node resolution
    void setVault(vault::Vault* vault) { vault_ = vault; }

    // Public interface (all callable from GUI thread)
    Q_INVOKABLE void open(quintptr nodeKey);
    Q_INVOKABLE void play() { setPlaying(true); }
    Q_INVOKABLE void pause() { setPlaying(false); }
    Q_INVOKABLE void seekBy(double s);
    Q_INVOKABLE void stop();

    [[nodiscard]] double position() const { return position_; }
    [[nodiscard]] double duration() const { return duration_; }
    [[nodiscard]] bool playing() const { return playing_; }
    [[nodiscard]] QString clockText() const;

signals:
    void positionChanged();
    void durationChanged();
    void playingChanged();
    void clockTextChanged();

private:
    void setPlaying(bool on);

    struct ControlMsg {
        enum class Type { Seek, Stop, PlayPause };
        Type type;
        double seekTarget = 0.0;
        bool play = false;
    };

    // Worker thread main loop
    void runWorker();

    // GUI thread: called by runWorker via queued invoke to render a frame
    void onFrameReady(std::shared_ptr<const FrameBox> frame);

    // --- GUI thread state ---
    VideoFrameItem* frameItem_ = nullptr;
    vault::Vault* vault_ = nullptr;
    double position_ = 0.0;
    double duration_ = 0.0;
    bool playing_ = false;

    // --- Shared state (guarded by mutex) ---
    mutable std::mutex mutex_;
    std::optional<ControlMsg> pendingControl_;

    // --- Worker thread state ---
    std::unique_ptr<media::ChunkAvio> avio_;
    std::unique_ptr<media::VideoDecoder> decoder_;
    std::unique_ptr<media::VideoDecodeWorker> worker_;
    uint64_t generation_ = 0;
    QElapsedTimer elapsed_;  // clock base for frame pacing
    double clockBase_ = 0.0;  // seek/rewind base PTS
    bool sentEof_ = false;
    std::jthread thread_;
};
