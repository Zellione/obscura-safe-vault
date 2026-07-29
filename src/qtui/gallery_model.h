#pragma once

#include <QAbstractListModel>
#include <QString>
#include <vector>

namespace vault {
    class Vault;
    struct IndexNode;
}

class ViewerController;
class PlaybackEngine;

// Gallery list model: displays galleries and media from vault().list(currentPath).
// Roles: name (QString), isGallery (bool), nodeKey (quintptr — opaque const IndexNode*).
// Invokables: enterGallery(int row), upOneLevel(), activate(int row).
// Property: currentPath (QString).
// Threading: main-thread-only; workers receive const IndexNode* pointers
// captured on main thread and call read_thumbnail only — never walk the tree.
class GalleryModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)
public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        IsGalleryRole,
        IsVideoRole,
        NodeKeyRole
    };
    Q_ENUM(Role)

    explicit GalleryModel(vault::Vault* vault, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Invokables for QML
    Q_INVOKABLE void enterGallery(int row);
    Q_INVOKABLE void upOneLevel();
    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE QString rename(int row, const QString& newName);
    Q_INVOKABLE quintptr nodeKeyAt(int row) const;

    [[nodiscard]] QString currentPath() const { return currentPath_; }

    // Set viewer controller for drain coordination (optional, for async image loading)
    void setViewerController(ViewerController* viewer) noexcept { viewerController_ = viewer; }

    // Set playback engine for drain coordination (optional, for video playback)
    void setPlaybackEngine(PlaybackEngine* playback) noexcept { playbackEngine_ = playback; }

    // Public refresh for programmatic update (e.g., after unlock)
    void refresh();

signals:
    void currentPathChanged();
    void openViewer(int row);  // emitted by activate(row) for images; Task 7 connects
    void openVideo(int row);   // emitted by activate(row) for videos; Task 9 connects

private:

    vault::Vault* vault_;
    ViewerController* viewerController_ = nullptr;
    PlaybackEngine* playbackEngine_ = nullptr;
    std::vector<const vault::IndexNode*> rows_;
    QString currentPath_;  // "/" for root, "/foo/bar" for nested
};
