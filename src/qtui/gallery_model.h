#pragma once

#include <QAbstractListModel>
#include <QString>
#include <memory>
#include <vector>

#include "vault/index.h"
#include "cover_provider.h"

namespace vault {
    class Vault;
    struct IndexNode;
}

class ViewerController;
class StatusController;

// Gallery list model: displays galleries and media from vault().list(currentPath).
// Roles: name (QString), isGallery (bool), nodeKey (quintptr — opaque const IndexNode*).
// Invokables: enterGallery(int row), upOneLevel(), activate(int row), nextSort(), prevSort().
// Properties: currentPath (QString), sortKey (int — vault::SortKey).
// Threading: main-thread-only; workers receive const IndexNode* pointers
// captured on main thread and call read_thumbnail only — never walk the tree.
class GalleryModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)
    Q_PROPERTY(int sortKey READ sortKey WRITE setSortKey NOTIFY sortKeyChanged)
public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        IsGalleryRole,
        IsVideoRole,
        NodeKeyRole,
        CoverRole,             // Task 2.4: CoverSpan vector for gallery tiles
        ChildCountsRole,       // Task 2.4: "X galleries · Y items" string
        IsAnimatedRole,        // Task 2.4: animated badge (format_can_animate AND animated flag)
        IsFavoriteRole         // Task 2.4: favorite gold-star (favorite bit)
    };
    Q_ENUM(Role)

    explicit GalleryModel(vault::Vault* vault, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Invokables for QML
    Q_INVOKABLE void enterGallery(int row);
    Q_INVOKABLE void upOneLevel();

    // Navigate directly to a gallery by path (T3.1 W5: quick-search / favorites /
    // advanced-search "open gallery" actions). Accepts both vault-style
    // ("foo/bar", as in SearchHit.path) and UI-style ("/foo/bar") paths;
    // "" and "/" mean root. Returns false (no state change) unless every
    // segment resolves to a gallery.
    Q_INVOKABLE bool navigateToPath(const QString& path);
    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE QString rename(int row, const QString& newName);
    Q_INVOKABLE quintptr nodeKeyAt(int row) const;

    // For SelectionController name-keying (Phase 58): get the name of row's node
    Q_INVOKABLE QString nameAt(int row) const;

    // Sort control (Task 2.1: Shift+S cycles forward, Shift+Cmd+S cycles backward)
    Q_INVOKABLE void nextSort();
    Q_INVOKABLE void prevSort();
    Q_INVOKABLE QString sortLabel() const;  // For breadcrumb display (Loose End 1)

    [[nodiscard]] QString currentPath() const { return currentPath_; }
    [[nodiscard]] int sortKey() const;
    void setSortKey(int key);

    // Set viewer controller for drain coordination (optional, for async image loading)
    void setViewerController(ViewerController* viewer) noexcept { viewerController_ = viewer; }

    // Set status controller for waste hint display (Scope: WS2.4)
    void setStatusController(StatusController* status) noexcept { statusController_ = status; }

    // Public refresh for programmatic update (e.g., after unlock).
    // Q_INVOKABLE: QML calls it after favorite toggles to update tile badges.
    Q_INVOKABLE void refresh();

    // Check and display waste hint (Scope: WS2.4)
    void checkAndDisplayWasteHint();

signals:
    void currentPathChanged();
    void sortKeyChanged();
    void openViewer(int row);  // emitted by activate(row) for images; Task 7 connects
    void openVideo(int row);   // emitted by activate(row) for videos; Task 9 connects

private:

    vault::Vault* vault_;
    ViewerController* viewerController_ = nullptr;
    StatusController* statusController_ = nullptr;    // Scope: WS2.4 for waste hint
    std::unique_ptr<CoverProvider> coverProvider_;  // Task 2.4: Cover cache
    std::vector<const vault::IndexNode*> rows_;
    QString currentPath_;  // "/" for root, "/foo/bar" for nested
    vault::SortKey sortKey_ = vault::SortKey::Default;
};
