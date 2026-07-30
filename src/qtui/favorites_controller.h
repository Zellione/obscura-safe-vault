#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <vector>

namespace vault {
class Vault;
struct SearchHit;
}

// QML-friendly wrapper for a favorite item (mirrors vault::SearchHit)
struct FavoriteItem {
    Q_GADGET
public:
    Q_PROPERTY(QString path MEMBER path)
    Q_PROPERTY(bool is_gallery MEMBER is_gallery)
    Q_PROPERTY(QString name MEMBER name)

    QString path;
    bool is_gallery = false;
    QString name;
};

Q_DECLARE_METATYPE(FavoriteItem)

class FavoritesController : public QObject {
    Q_OBJECT

public:
    explicit FavoritesController(QObject* parent = nullptr);

    // Set the vault pointer (called during app context setup)
    void setVault(vault::Vault* v) noexcept { vault_ = v; }

    // Toggle the favorite flag on a node (image or gallery).
    // Returns true on success, false on vault error.
    Q_INVOKABLE bool toggleFavorite(const QString& nodePath);

    // Get all favorited images as a flat list.
    // Each item has path, is_gallery=false, name, and node pointer.
    Q_INVOKABLE QList<FavoriteItem> getFavoriteImages() const;

    // Get all favorited galleries as a flat list.
    // Each item has path, is_gallery=true, name, and node pointer.
    Q_INVOKABLE QList<FavoriteItem> getFavoriteGalleries() const;

    // Check if a node is currently favorited.
    Q_INVOKABLE bool isFavorite(const QString& nodePath) const;

signals:
    // Emitted when the favorite state of any node changes
    void favoritesChanged();

private:
    vault::Vault* vault_ = nullptr;

    // Helper: check if vault is unlocked and ready
    bool isVaultReady() const;

    // Convert vault::SearchHit to QML-friendly FavoriteItem
    FavoriteItem searchHitToItem(const vault::SearchHit& hit) const;
};
