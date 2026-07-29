#include "gallery_model.h"
#include "thumb_cache.h"
#include "vault/vault.h"

GalleryModel::GalleryModel(vault::Vault* vault, QObject* parent)
    : QAbstractListModel(parent), vault_(vault), currentPath_("/")
{
    if (vault_ && vault_->is_unlocked()) {
        refresh();
    }
}

int GalleryModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;  // no tree structure, flat list
    return static_cast<int>(rows_.size());
}

QVariant GalleryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount())
        return QVariant();

    const auto* node = rows_[index.row()];
    if (!node)
        return QVariant();

    switch (role) {
        case NameRole:
            return QString::fromStdString(node->name);
        case IsGalleryRole:
            return node->type == vault::IndexNode::Type::Gallery;
        case NodeKeyRole:
            // Opaque pointer, safe to pass as quintptr because workers
            // only use it with ThumbCache.request(key), never dereference
            return QVariant::fromValue(static_cast<quintptr>(reinterpret_cast<uintptr_t>(node)));
        default:
            return QVariant();
    }
}

QHash<int, QByteArray> GalleryModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[IsGalleryRole] = "isGallery";
    roles[NodeKeyRole] = "nodeKey";
    return roles;
}

void GalleryModel::refresh()
{
    // Drain pending thumbnail workers before rebuilding rows.
    // Workers must never outlive the row snapshot that produced their node pointers.
    // This ensures all in-flight workers (who hold pointers to old tree nodes) complete
    // before we discard rows_ and the old IndexNode* pointers become stale.
    auto cache = ThumbCache::instance();
    if (cache) {
        cache->drainPending();
    }

    beginResetModel();
    rows_.clear();

    if (!vault_ || !vault_->is_unlocked()) {
        endResetModel();
        return;
    }

    // Get all nodes from vault at current path
    const auto all_nodes = vault_->list(currentPath_.toStdString());

    // Partition: galleries first, then media
    std::vector<const vault::IndexNode*> galleries, media;
    for (const auto* node : all_nodes) {
        if (!node)
            continue;
        if (node->type == vault::IndexNode::Type::Gallery) {
            galleries.push_back(node);
        } else {
            media.push_back(node);
        }
    }

    // Combine: galleries first, then media
    rows_.reserve(galleries.size() + media.size());
    rows_.insert(rows_.end(), galleries.begin(), galleries.end());
    rows_.insert(rows_.end(), media.begin(), media.end());

    endResetModel();
}

void GalleryModel::enterGallery(int row)
{
    if (row < 0 || row >= rowCount())
        return;

    const auto* node = rows_[row];
    if (!node || node->type != vault::IndexNode::Type::Gallery)
        return;

    // Append gallery name to current path
    // currentPath_ format: "/" or "/foo" or "/foo/bar"
    if (currentPath_ != "/") {
        currentPath_ += "/";
    }
    currentPath_ += QString::fromStdString(node->name);

    refresh();
    emit currentPathChanged();
}

void GalleryModel::upOneLevel()
{
    if (currentPath_ == "/")
        return;  // already at root

    // Remove last component
    // "/foo" → "/"
    // "/foo/bar" → "/foo"
    int lastSlash = currentPath_.lastIndexOf("/");
    if (lastSlash == 0) {
        currentPath_ = "/";
    } else {
        currentPath_ = currentPath_.left(lastSlash);
    }

    refresh();
    emit currentPathChanged();
}

void GalleryModel::activate(int row)
{
    if (row < 0 || row >= rowCount())
        return;

    const auto* node = rows_[row];
    if (!node)
        return;

    if (node->type == vault::IndexNode::Type::Gallery) {
        enterGallery(row);
    } else {
        // Image or Video: signal to open viewer (Task 7 will connect this)
        emit openViewer(row);
    }
}
