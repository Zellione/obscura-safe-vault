#include "autolock.h"

#include <QCoreApplication>
#include <QEvent>
#include <QTimer>

#include "unlock_controller.h"

AutoLock::AutoLock(QObject* parent)
    : QObject(parent)
{
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &AutoLock::onTimerFired);

    // Install global event filter to detect user input
    qApp->installEventFilter(this);
}

AutoLock::~AutoLock()
{
    qApp->removeEventFilter(this);
}

void AutoLock::setUnlockController(UnlockController* controller) noexcept
{
    unlockController_ = controller;
}

void AutoLock::setIdleSeconds(int secs)
{
    if (secs != idleSeconds_) {
        idleSeconds_ = secs;
        restartTimer();
        emit idleSecondsChanged();
    }
}

void AutoLock::setKeepUnlocked(bool keep)
{
    if (keep != keepUnlocked_) {
        keepUnlocked_ = keep;
        if (keep) {
            stopTimer();
        } else {
            restartTimer();
        }
        emit keepUnlockedChanged();
    }
}

void AutoLock::setQueueCount(int count)
{
    if (count != queueCount_) {
        queueCount_ = count;
        // If queue becomes non-empty, suppress autolock
        if (count > 0 && !suppressed_) {
            setSuppressed(true);
        }
        // If queue becomes empty, un-suppress autolock
        else if (count == 0 && suppressed_) {
            setSuppressed(false);
        }
        emit queueCountChanged();
    }
}

void AutoLock::setSuppressed(bool suppressed)
{
    if (suppressed != suppressed_) {
        suppressed_ = suppressed;
        if (suppressed) {
            stopTimer();
        } else {
            restartTimer();
        }
    }
}

bool AutoLock::eventFilter(QObject* obj, QEvent* event)
{
    // Detect user input events and restart the idle timer
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::MouseMove ||
        event->type() == QEvent::MouseButtonPress || event->type() == QEvent::Wheel) {
        if (!keepUnlocked_ && !suppressed_) {
            restartTimer();
        }
    }
    return QObject::eventFilter(obj, event);
}

void AutoLock::onTimerFired()
{
    // Lock the vault and return to the unlock screen
    if (unlockController_ && unlockController_->unlocked()) {
        unlockController_->lock();
    }
}

void AutoLock::restartTimer()
{
    if (keepUnlocked_ || suppressed_ || idleSeconds_ <= 0) {
        stopTimer();
        return;
    }
    timer_.stop();
    timer_.start(idleSeconds_ * 1000);
}

void AutoLock::stopTimer()
{
    timer_.stop();
}
