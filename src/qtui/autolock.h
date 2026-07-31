#pragma once

#include <QObject>
#include <QTimer>
#include <QPointer>

class UnlockController;

// Idle auto-lock timer: locks the vault when no user input for N seconds.
// Session-scoped state (NOT persisted in settings).
// Suppressed while import queue is non-empty (WS4 contract).
class AutoLock : public QObject {
    Q_OBJECT
    Q_PROPERTY(int idleSeconds READ idleSeconds WRITE setIdleSeconds NOTIFY idleSecondsChanged)
    Q_PROPERTY(bool keepUnlocked READ keepUnlocked WRITE setKeepUnlocked NOTIFY keepUnlockedChanged)
    Q_PROPERTY(int queueCount READ queueCount WRITE setQueueCount NOTIFY queueCountChanged)
public:
    explicit AutoLock(QObject* parent = nullptr);
    ~AutoLock();

    void setUnlockController(UnlockController* controller) noexcept;

    [[nodiscard]] int idleSeconds() const { return idleSeconds_; }
    void setIdleSeconds(int secs);

    [[nodiscard]] bool keepUnlocked() const { return keepUnlocked_; }
    void setKeepUnlocked(bool keep);

    [[nodiscard]] int queueCount() const { return queueCount_; }
    void setQueueCount(int count);

    // Set suppression state (called by WS4 when import queue changes)
    Q_INVOKABLE void setSuppressed(bool suppressed);

signals:
    void idleSecondsChanged();
    void keepUnlockedChanged();
    void queueCountChanged();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onTimerFired();

private:
    void restartTimer();
    void stopTimer();

    QPointer<UnlockController> unlockController_;
    QTimer timer_;
    int idleSeconds_ = 300;  // default: 5 minutes
    bool keepUnlocked_ = false;
    int queueCount_ = 0;
    bool suppressed_ = false;
};
