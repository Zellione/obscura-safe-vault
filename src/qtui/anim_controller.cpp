#include "anim_controller.h"

AnimController::AnimController(QObject* parent)
    : QObject(parent), playing_(false)
{
    timer_.setInterval(16);  // ~60 FPS
    connect(&timer_, &QTimer::timeout, this, &AnimController::onTick);
}

AnimController::~AnimController() {
    timer_.stop();
}

void AnimController::play() {
    if (!playing_) {
        playing_ = true;
        emit playingChanged();
        timer_.start();
    }
}

void AnimController::pause() {
    if (playing_) {
        playing_ = false;
        emit playingChanged();
        timer_.stop();
    }
}

void AnimController::togglePlayPause() {
    if (playing_) {
        pause();
    } else {
        play();
    }
}

void AnimController::setTotalFrames(int count) {
    if (totalFrames_ != count) {
        totalFrames_ = count;
        emit totalFramesChanged();
    }
}

void AnimController::reset() {
    currentFrame_ = 0;
    accumulator_ = 0.0;
    emit currentFrameChanged();
}

void AnimController::onTick() {
    if (!playing_ || totalFrames_ <= 1) {
        return;
    }

    // Advance time accumulator by ~16.67 ms
    const double dt = 0.01667;
    accumulator_ += dt;

    // Determine how many frames to advance
    int framesToAdvance = 0;
    if (accumulator_ >= currentFrameDelay_) {
        framesToAdvance = static_cast<int>(accumulator_ / currentFrameDelay_);
        accumulator_ -= framesToAdvance * currentFrameDelay_;

        // Cap catch-up to 64 frames to prevent runaway after stalls
        if (framesToAdvance > 64) {
            framesToAdvance = 64;
        }
    }

    if (framesToAdvance > 0) {
        // Advance frame index with wrapping
        currentFrame_ = (currentFrame_ + framesToAdvance) % totalFrames_;
        emit currentFrameChanged();
        emit frameAdvance(framesToAdvance);
    }
}
