#pragma once

#include <QObject>
#include <QString>

#include "ui/gallery_session_state.h"

// QObject adapter over ui::GallerySessionState, exposing per-path focus
// tracking, view density, detail panel state, thumbnail strip side, and
// video resume bookmark.
class SessionState : public QObject {
    Q_OBJECT

public:
    explicit SessionState(QObject* parent = nullptr);

    // Per-path focused tile index (Phase 40 Part 2)
    void recordFocusIndex(const QString& path, int index);
    [[nodiscard]] int recallFocusIndex(const QString& path) const;

    // Global view density (last-used gallery grid density)
    [[nodiscard]] int viewDensity() const;
    void setViewDensity(int density);

    // Global detail panel toggle state
    [[nodiscard]] bool detailOpen() const;
    void setDetailOpen(bool open);

    // Global thumbnail strip side (0=Bottom, 1=Right, ...)
    [[nodiscard]] int stripSide() const;
    void setStripSide(int side);

    // Video resume bookmark (last-paused video + position)
    [[nodiscard]] QString lastMediaPath() const;
    void setLastMediaPath(const QString& path);

    [[nodiscard]] double videoResumeSeconds() const;
    void setVideoResumeSeconds(double seconds);

    // Custom session data (arbitrary key-value pairs; e.g., query state)
    Q_INVOKABLE void recordCustomData(const QString& key, const QString& value);
    Q_INVOKABLE QString recallCustomData(const QString& key) const;

    // Clear all session state
    void reset();

signals:
    void focusIndexChanged(const QString& path, int index);
    void viewDensityChanged();
    void detailOpenChanged();
    void stripSideChanged();
    void videoResumeChanged();

private:
    ui::GallerySessionState state_;
};
