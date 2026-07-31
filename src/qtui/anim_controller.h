#pragma once

#include <QObject>
#include <QTimer>
#include <memory>

// Qt wrapper for animated image playback (GIF/WebP).
// Manages frame scheduling via ui::anim_model, integrates with SecureImageItem.
//
// The animated image decoder produces frames on-demand; the controller
// advances the frame clock and signals when a new frame should be decoded.
//
// Note (Phase 47): Full integration with SecureImageItem and the pixel buffer
// decode path is pending. For now, this provides the timing/scheduling API.
class AnimController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(int currentFrame READ currentFrame NOTIFY currentFrameChanged)
    Q_PROPERTY(int totalFrames READ totalFrames NOTIFY totalFramesChanged)

public:
    explicit AnimController(QObject* parent = nullptr);
    ~AnimController();

    // Properties
    [[nodiscard]] bool playing() const { return playing_; }
    [[nodiscard]] int currentFrame() const { return currentFrame_; }
    [[nodiscard]] int totalFrames() const { return totalFrames_; }

    // Invokables
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlayPause();
    Q_INVOKABLE void setTotalFrames(int count);
    Q_INVOKABLE void reset();

signals:
    void playingChanged();
    void currentFrameChanged();
    void totalFramesChanged();
    void frameAdvance(int frames);  // decoder should pull this many frames

private slots:
    void onTick();

private:
    bool playing_ = true;
    int currentFrame_ = 0;
    int totalFrames_ = 0;
    double accumulator_ = 0.0;
    double currentFrameDelay_ = 0.04;  // default ~25 fps

    QTimer timer_;
};
