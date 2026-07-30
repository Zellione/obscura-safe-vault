#pragma once

#include <QObject>
#include <QTimer>
#include <memory>
#include "ui/slideshow_model.h"

// Qt wrapper around ui::SlideshowModel for QML integration.
// Provides properties and signals for slideshow state, wraps the headless state machine.
class SlideshowController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int previousIndex READ previousIndex NOTIFY fadeProgressChanged)
    Q_PROPERTY(double fadeProgress READ fadeProgress NOTIFY fadeProgressChanged)
    Q_PROPERTY(double dwell READ dwell NOTIFY dwellChanged)
    Q_PROPERTY(bool animating READ animating NOTIFY animatingChanged)

public:
    explicit SlideshowController(int galleryItemCount, int startIndex = 0, QObject* parent = nullptr);
    ~SlideshowController();

    // Properties
    [[nodiscard]] bool running() const;
    [[nodiscard]] int currentIndex() const;
    [[nodiscard]] int previousIndex() const;
    [[nodiscard]] double fadeProgress() const;
    [[nodiscard]] double dwell() const;
    [[nodiscard]] bool animating() const;

    // Invokables
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void setRunning(bool running);
    Q_INVOKABLE void adjustDwell(double delta);
    Q_INVOKABLE void advance(int delta);

signals:
    void runningChanged();
    void currentIndexChanged();
    void fadeProgressChanged();
    void dwellChanged();
    void animatingChanged();

private slots:
    void onTick();

private:
    std::unique_ptr<ui::SlideshowModel> model_;
    QTimer timer_;
    int lastIndex_ = -1;
    bool lastAnimating_ = false;
};
