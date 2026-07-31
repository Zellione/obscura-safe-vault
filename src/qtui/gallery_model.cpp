#include "gallery_model.h"
#include "cover_provider.h"
#include "status_controller.h"
#include "thumb_cache.h"
#include "viewer_controller.h"
#include "vault/vault.h"
#include "vault/safe_name.h"
#include "ui/gallery_sort.h"
#include "ui/child_counts.h"
#include "ui/waste_threshold.h"

#include <span>

namespace {

// T3.1 W5 crash fix: the representative cover NODE for a gallery tile.
// Mirrors ui::resolve_single_cover's traversal (gallery_cover.cpp) but yields
// the node instead of a chunk span — the Qt thumb pipeline (SecureImageItem →
// ThumbCache) is keyed by IndexNode*, and feeding it a chunk offset made
// ThumbCache reinterpret_cast a file offset into a node pointer (segfault on
// unlock for any gallery tile with a cover).
const vault::IndexNode* resolveCoverNode(const vault::IndexNode& gallery, uint32_t max_depth)
{
    if (max_depth == 0) return nullptr;
    for (const auto& child : gallery.children) {
        if (child.is_image() && child.meta.thumb_length > 0) return &child;
        if (child.is_video() && child.vmeta.poster_length > 0) return &child;
        if (child.is_gallery())
            if (const auto* node = resolveCoverNode(child, max_depth - 1)) return node;
    }
    return nullptr;
}

}  // namespace

GalleryModel::GalleryModel(vault::Vault* vault, QObject* parent)
    : QAbstractListModel(parent), vault_(vault), currentPath_("/"),
      coverProvider_(std::make_unique<CoverProvider>(this))
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
        case CoverRole: {
            // Task 2.4 / T3.1 W5: the representative cover NODE's key for
            // gallery tiles (renders through the normal SecureImageItem →
            // ThumbCache path, exactly like a media tile's own thumbnail).
            // Only galleries have covers; media files return empty.
            if (node->type != vault::IndexNode::Type::Gallery)
                return QVariant();

            const auto* cover = resolveCoverNode(*node, vault::INDEX_MAX_DEPTH);
            if (cover == nullptr)
                return QVariant();  // Empty gallery, fall back to folder icon

            return QVariant::fromValue(
                static_cast<quintptr>(reinterpret_cast<uintptr_t>(cover)));
        }
        case ChildCountsRole: {
            // Task 2.4: Return "X galleries · Y items" string for galleries only
            if (node->type != vault::IndexNode::Type::Gallery)
                return QVariant();

            const auto counts = ui::direct_child_counts(*node);
            const auto label = ui::format_tile_counts(counts);
            return QString::fromStdString(label);
        }
        case IsAnimatedRole: {
            // Task 2.4: Badge for GIF/WebP with animated flag set
            // Returns true only if format_can_animate(format) AND animated flag
            if (node->type != vault::IndexNode::Type::Image)
                return false;

            // Check if format can animate (GIF or WebP)
            if (!vault::format_can_animate(node->meta.format))
                return false;

            // Check animated flag
            return node->meta.animated;
        }
        case IsFavoriteRole: {
            // Task 2.4: Gold-star for favorite bit (read-only, WS5 provides toggle)
            return node->favorite;
        }
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
    roles[CoverRole] = "cover";                  // Task 2.4
    roles[ChildCountsRole] = "childCounts";      // Task 2.4
    roles[IsAnimatedRole] = "isAnimated";        // Task 2.4
    roles[IsFavoriteRole] = "isFavorite";        // Task 2.4
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

    // Task 2.4: Clear cover cache on refresh (invalidate memoised covers)
    if (coverProvider_) {
        coverProvider_->clear();
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

    // Scope: WS2.4 — Check and display waste hint on refresh
    checkAndDisplayWasteHint();
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

bool GalleryModel::navigateToPath(const QString& path)
{
    if (!vault_ || !vault_->is_unlocked())
        return false;

    // Normalize to segment list (drops leading/trailing/duplicate slashes),
    // matching what find_gallery's split_path resolves.
    const QStringList segments = path.split('/', Qt::SkipEmptyParts);
    const QString normalized = "/" + segments.join('/');

    const auto* node =
        static_cast<const vault::Vault*>(vault_)->resolve_node(path.toStdString());
    if (node == nullptr || !node->is_gallery())
        return false;  // missing, or a media node

    if (normalized == currentPath_)
        return true;  // already there; skip the refresh

    currentPath_ = normalized;
    refresh();
    emit currentPathChanged();
    return true;
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

QString GalleryModel::sortLabel() const
{
    if (!vault_)
        return QString();

    // Get the vault default sort and generate the label for breadcrumb display
    const auto& settings = vault::vault_settings(*vault_);
    auto label = ui::sort_key_label(sortKey_, settings.default_sort);
    return QString::fromStdString(label);
}

void GalleryModel::checkAndDisplayWasteHint()
{
    if (!vault_ || !statusController_)
        return;

    // Get waste info from vault
    const auto wasted = vault_->wasted_bytes();
    const auto file_size = vault::vault_file_bytes(*vault_);

    // Check if waste exceeds threshold (Scope: WS2.4)
    if (ui::should_display_waste(wasted, file_size)) {
        // Display hint with Shift+C compact shortcut
        statusController_->set(0, "Vault has unused space. Press Shift+C to compact.");
    }
}

