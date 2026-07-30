#include "session_state.h"

SessionState::SessionState(QObject* parent) : QObject(parent)
{
}

void SessionState::recordFocusIndex(const QString& path, int index)
{
    state_.record(path.toStdString(), index);
    emit focusIndexChanged(path, index);
}

int SessionState::recallFocusIndex(const QString& path) const
{
    return state_.recall(path.toStdString());
}

int SessionState::viewDensity() const
{
    // GalleryView is an enum; convert to int (0=GridS, 1=GridM, 2=GridL, 3=Strip)
    return static_cast<int>(state_.view);
}

void SessionState::setViewDensity(int density)
{
    if (density >= 0 && density < 4 && static_cast<int>(state_.view) != density) {
        state_.view = static_cast<ui::GalleryView>(density);
        emit viewDensityChanged();
    }
}

bool SessionState::detailOpen() const
{
    return state_.detail_open;
}

void SessionState::setDetailOpen(bool open)
{
    if (state_.detail_open != open) {
        state_.detail_open = open;
        emit detailOpenChanged();
    }
}

int SessionState::stripSide() const
{
    // StripSide is an enum; convert to int
    return static_cast<int>(state_.strip_side);
}

void SessionState::setStripSide(int side)
{
    if (side >= 0 && side < 4 && static_cast<int>(state_.strip_side) != side) {
        state_.strip_side = static_cast<ui::StripSide>(side);
        emit stripSideChanged();
    }
}

QString SessionState::lastMediaPath() const
{
    return QString::fromStdString(state_.last_media_path);
}

void SessionState::setLastMediaPath(const QString& path)
{
    if (QString::fromStdString(state_.last_media_path) != path) {
        state_.last_media_path = path.toStdString();
        emit videoResumeChanged();
    }
}

double SessionState::videoResumeSeconds() const
{
    return state_.video_resume_seconds;
}

void SessionState::setVideoResumeSeconds(double seconds)
{
    if (state_.video_resume_seconds != seconds) {
        state_.video_resume_seconds = seconds;
        emit videoResumeChanged();
    }
}

void SessionState::recordCustomData(const QString& key, const QString& value)
{
    state_.record_custom_data(key.toStdString(), value.toStdString());
}

QString SessionState::recallCustomData(const QString& key) const
{
    return QString::fromStdString(state_.recall_custom_data(key.toStdString()));
}

void SessionState::reset()
{
    state_.reset();
    emit focusIndexChanged("", 0);
    emit viewDensityChanged();
    emit detailOpenChanged();
    emit stripSideChanged();
    emit videoResumeChanged();
}
