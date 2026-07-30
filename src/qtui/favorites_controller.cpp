#include "favorites_controller.h"

#include "vault/vault.h"

FavoritesController::FavoritesController(QObject* parent)
    : QObject(parent)
{
    // Register metatypes for QML
    qRegisterMetaType<FavoriteItem>();
}

bool FavoritesController::isVaultReady() const
{
    return vault_ != nullptr && vault_->is_unlocked();
}

bool FavoritesController::toggleFavorite(const QString& nodePath)
{
    if (!isVaultReady()) {
        return false;
    }

    auto result = vault_->toggle_favorite(nodePath.toStdString());
    if (result == vault::VaultResult::Ok) {
        emit favoritesChanged();
        return true;
    }
    return false;
}

QList<FavoriteItem> FavoritesController::getFavoriteImages() const
{
    QList<FavoriteItem> result;
    if (!isVaultReady()) {
        return result;
    }

    auto hits = vault_->list_favorite_images();
    for (const auto& hit : hits) {
        result.append(searchHitToItem(hit));
    }
    return result;
}

QList<FavoriteItem> FavoritesController::getFavoriteGalleries() const
{
    QList<FavoriteItem> result;
    if (!isVaultReady()) {
        return result;
    }

    auto hits = vault_->list_favorite_galleries();
    for (const auto& hit : hits) {
        result.append(searchHitToItem(hit));
    }
    return result;
}

bool FavoritesController::isFavorite(const QString& nodePath) const
{
    if (!isVaultReady()) {
        return false;
    }

    // Normalize the nodePath by removing leading slash if present for comparison
    std::string pathToCheck = nodePath.toStdString();
    if (!pathToCheck.empty() && pathToCheck[0] == '/') {
        pathToCheck = pathToCheck.substr(1);
    }

    // Check both images and galleries for the favorite flag
    auto imgFavs = vault_->list_favorite_images();
    for (const auto& hit : imgFavs) {
        if (hit.path == pathToCheck) {
            return true;
        }
    }

    auto galFavs = vault_->list_favorite_galleries();
    for (const auto& hit : galFavs) {
        if (hit.path == pathToCheck) {
            return true;
        }
    }

    return false;
}

FavoriteItem FavoritesController::searchHitToItem(const vault::SearchHit& hit) const
{
    FavoriteItem item;
    item.path = QString::fromStdString(hit.path);
    item.is_gallery = hit.is_gallery;
    item.name = QString::fromStdString(hit.name);
    return item;
}
