#include "slideshow_controller.h"

SlideshowController::SlideshowController(int galleryItemCount, int startIndex, QObject* parent)
    : QObject(parent),
      model_(std::make_unique<ui::SlideshowModel>(galleryItemCount, startIndex,
                                                    ui::SLIDESHOW_DWELL_DEFAULT,
                                                    false, 0))  // no shuffle, no seed
{
    // Timer ticks at 60 Hz (~16.67 ms per frame)
    timer_.setInterval(16);
    connect(&timer_, &QTimer::timeout, this, &SlideshowController::onTick);

    // Start the timer (slideshow runs by default)
    timer_.start();
}

SlideshowController::~SlideshowController() {
    timer_.stop();
}

bool SlideshowController::running() const {
    return model_->running();
}

int SlideshowController::currentIndex() const {
    return model_->index();
}

int SlideshowController::previousIndex() const {
    return model_->prev_index();
}

double SlideshowController::fadeProgress() const {
    return model_->fade_progress();
}

double SlideshowController::dwell() const {
    return model_->dwell();
}

bool SlideshowController::animating() const {
    return model_->animating();
}

void SlideshowController::toggle() {
    model_->toggle();
    emit runningChanged();
}

void SlideshowController::setRunning(bool running) {
    if (model_->running() != running) {
        model_->set_running(running);
        emit runningChanged();
    }
}

void SlideshowController::adjustDwell(double delta) {
    const double oldDwell = model_->dwell();
    model_->adjust_dwell(delta);
    if (oldDwell != model_->dwell()) {
        emit dwellChanged();
    }
}

void SlideshowController::advance(int delta) {
    const int oldIndex = model_->index();
    model_->advance(delta);
    if (oldIndex != model_->index()) {
        emit currentIndexChanged();
        emit fadeProgressChanged();
    }
}

void SlideshowController::onTick() {
    // Advance time by ~16.67 ms
    const double dt = 0.01667;  // 1/60 second
    const bool indexChanged = model_->tick(dt);

    if (lastIndex_ != model_->index()) {
        lastIndex_ = model_->index();
        emit currentIndexChanged();
    }

    if (lastAnimating_ != model_->animating()) {
        lastAnimating_ = model_->animating();
        emit animatingChanged();
    }

    // Always emit fade progress and running changes
    emit fadeProgressChanged();

    // If animating is no longer needed, we can stop the timer (but keep ticking for smooth fades)
    // For now, keep it running to handle fades smoothly
}
