#include "gallery_model.h"
#include "thumb_cache.h"
#include "viewer_controller.h"
#include "vault/vault.h"
#include "vault/safe_name.h"
#include "ui/gallery_sort.h"

#include <span>

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
        case IsVideoRole:
            return node->type == vault::IndexNode::Type::Video;
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
    roles[IsVideoRole] = "isVideo";
    roles[NodeKeyRole] = "nodeKey";
    return roles;
}

void GalleryModel::refresh()
{
    // Drain pending workers before rebuilding rows.
    // Workers (both thumbnail and full-image viewers) must never outlive the row snapshot
    // that produced their node pointers. This ensures all in-flight workers complete
    // before we discard rows_ and the old IndexNode* pointers become stale.
    auto cache = ThumbCache::instance();
    if (cache) {
        cache->drainPending();
    }
    if (viewerController_) {
        viewerController_->shutdownAndDrain();
    }

    beginResetModel();
    rows_.clear();

    if (!vault_ || !vault_->is_unlocked()) {
        endResetModel();
        return;
    }

    // Get all nodes from vault at current path
    auto all_nodes = vault_->list(currentPath_.toStdString());

    // Sort using the current sort key (convert vector to span for sort_children)
    auto sorted = ui::sort_children(all_nodes, sortKey_);

    // Partition sorted results into galleries and media, preserving sort order within each group
    std::vector<const vault::IndexNode*> galleries, media;
    for (const auto* node : sorted) {
        if (!node)
            continue;
        if (node->type == vault::IndexNode::Type::Gallery) {
            galleries.push_back(node);
        } else {
            media.push_back(node);
        }
    }

    // Combine: galleries first, then media (both maintain their sort order)
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
    } else if (node->type == vault::IndexNode::Type::Video) {
        // Video: route to video playback (Task 9)
        emit openVideo(row);
    } else {
        // Image: route to image viewer (Task 7)
        emit openViewer(row);
    }
}

QString GalleryModel::rename(int row, const QString& newName)
{
    // Validate input
    if (row < 0 || row >= rowCount())
        return "Invalid row";

    const auto* node = rows_[row];
    if (!node)
        return "Node not found";

    // Check if the name is safe
    if (!vault::is_safe_node_name(newName.toStdString())) {
        return "Invalid name";
    }

    // Call vault::rename_node
    const auto result = vault::rename_node(
        *vault_,
        currentPath_.toStdString(),
        node->name,
        newName.toStdString()
    );

    if (result != vault::VaultResult::Ok) {
        return "Rename failed";
    }

    // Refresh to pick up the change
    refresh();

    return "";  // empty string = success
}

quintptr GalleryModel::nodeKeyAt(int row) const
{
    if (row < 0 || row >= rowCount())
        return 0;

    const auto* node = rows_[row];
    if (!node)
        return 0;

    return static_cast<quintptr>(reinterpret_cast<uintptr_t>(node));
}

QString GalleryModel::nameAt(int row) const
{
    if (row < 0 || row >= rowCount())
        return QString();

    const auto* node = rows_[row];
    if (!node)
        return QString();

    return QString::fromStdString(node->name);
}

void GalleryModel::nextSort()
{
    setSortKey(static_cast<int>(ui::next_sort_key(sortKey_)));
}

void GalleryModel::prevSort()
{
    setSortKey(static_cast<int>(ui::prev_sort_key(sortKey_)));
}

int GalleryModel::sortKey() const
{
    return static_cast<int>(sortKey_);
}

void GalleryModel::setSortKey(int key)
{
    auto newKey = static_cast<vault::SortKey>(key);
    if (newKey == sortKey_)
        return;

    sortKey_ = newKey;
    emit sortKeyChanged();

    // Refresh with new sort order
    refresh();
}

