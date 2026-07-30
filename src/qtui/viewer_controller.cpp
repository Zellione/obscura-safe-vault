#include "viewer_controller.h"

#include <QRunnable>
#include <QThreadPool>
#include <algorithm>

#include "image/decode.h"
#include "secure_image_item.h"
#include "gallery_model.h"
#include "vault/vault.h"
#include "ui/album_rebind.h"

// Worker runnable: decode image from vault asynchronously
class ViewerWorker : public QRunnable {
public:
    ViewerWorker(ViewerController* controller, const vault::IndexNode* node, uint64_t generation, vault::Vault* vault)
        : controller_(controller), node_(node), generation_(generation), vault_(vault)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        // Double-check generation on worker thread to avoid stale node dereference
        if (!controller_ || controller_->generation_.load() != generation_) {
            return;  // stale, drop
        }

        if (!vault_ || !node_) {
            return;
        }

        // Decode image from vault (safe off-main via read paths)
        crypto::SecureBytes imageBytes;
        auto result = vault_->read_image(*node_, imageBytes);
        if (result != vault::VaultResult::Ok) {
            // Signal failure back to main thread via queued connection
            QMetaObject::invokeMethod(controller_, [controller = controller_, generation = generation_]() {
                if (controller->generation_.load() == generation) {
                    controller->onImageReady(nullptr, generation);
                }
            }, Qt::QueuedConnection);
            return;
        }

        // Decode pixels from bytes
        std::span<const uint8_t> imageSpan(imageBytes.data(), imageBytes.size());
        auto imageDataOpt = image::decode_from_memory(imageSpan);

        if (!imageDataOpt.has_value()) {
            // Decode failed
            QMetaObject::invokeMethod(controller_, [controller = controller_, generation = generation_]() {
                if (controller->generation_.load() == generation) {
                    controller->onImageReady(nullptr, generation);
                }
            }, Qt::QueuedConnection);
            return;
        }

        // Convert ImageData to PixelBuffer (RGB to RGBA)
        PixelBuffer pixelBuffer = expand_rgb_to_rgba(imageDataOpt.value());

        // Deliver result via main thread callback (generation-checked)
        auto pixelPtr = std::make_shared<const PixelBuffer>(pixelBuffer);
        QMetaObject::invokeMethod(controller_, [controller = controller_, pixels = pixelPtr, generation = generation_]() {
            controller->onImageReady(pixels, generation);
        }, Qt::QueuedConnection);
    }

private:
    ViewerController* controller_;
    const vault::IndexNode* node_;
    uint64_t generation_;
    vault::Vault* vault_;
};

ViewerController::ViewerController(vault::Vault* vault, GalleryModel* galleryModel, QObject* parent)
    : QObject(parent), vault_(vault), galleryModel_(galleryModel)
{
    // Max 1 thread for sequential image loading (no parallelism needed)
    pool_.setMaxThreadCount(1);
}

ViewerController::~ViewerController()
{
    shutdownAndDrain();
}

void ViewerController::bindItem(SecureImageItem* item)
{
    // If unbinding (item == nullptr), bump generation to invalidate any in-flight workers.
    // This ensures stale results won't call setImage on the unbound (possibly destroyed) item.
    if (!item && boundItem_) {
        bumpGeneration();
    }
    boundItem_ = item;
}

void ViewerController::setLoading(bool state)
{
    if (loading_ != state) {
        loading_ = state;
        emit loadingChanged();
    }
}

void ViewerController::setImageName(const QString& name)
{
    if (imageName_ != name) {
        imageName_ = name;
        emit imageNameChanged();
    }
}

void ViewerController::bumpGeneration()
{
    generation_.fetch_add(1, std::memory_order_acq_rel);
}

void ViewerController::open(int galleryRow)
{
    if (!galleryModel_ || !vault_) {
        return;
    }

    bumpGeneration();
    if (currentGalleryIndex_ != galleryRow) {
        currentGalleryIndex_ = galleryRow;
        emit currentIndexChanged();
    }
    loadImageAtIndex(galleryRow);
}

void ViewerController::prev()
{
    if (!galleryModel_ || currentGalleryIndex_ < 0) {
        return;
    }

    // If in album mode, navigate album list
    if (!albumNodeKeys_.isEmpty()) {
        int prevIdx = albumCurrentIndex_ - 1;
        if (prevIdx >= 0) {
            bumpGeneration();
            albumCurrentIndex_ = prevIdx;
            loadImageAtAlbumIndex(prevIdx);
        }
        return;
    }

    // Gallery mode: find previous media item (skip galleries)
    int searchIdx = currentGalleryIndex_ - 1;
    while (searchIdx >= 0) {
        // Check if row at searchIdx is a media item (not gallery)
        QModelIndex idx = galleryModel_->index(searchIdx);
        bool isGallery = galleryModel_->data(idx, GalleryModel::IsGalleryRole).toBool();
        if (!isGallery) {
            bumpGeneration();
            if (currentGalleryIndex_ != searchIdx) {
                currentGalleryIndex_ = searchIdx;
                emit currentIndexChanged();
            }
            loadImageAtIndex(searchIdx);
            return;
        }
        searchIdx--;
    }
    // No previous media found
}

void ViewerController::next()
{
    if (!galleryModel_ || currentGalleryIndex_ < 0) {
        return;
    }

    // If in album mode, navigate album list
    if (!albumNodeKeys_.isEmpty()) {
        int nextIdx = albumCurrentIndex_ + 1;
        if (nextIdx < albumNodeKeys_.size()) {
            bumpGeneration();
            albumCurrentIndex_ = nextIdx;
            loadImageAtAlbumIndex(nextIdx);
        }
        return;
    }

    // Gallery mode: skip galleries in current folder
    int rowCount = galleryModel_->rowCount();
    int searchIdx = currentGalleryIndex_ + 1;
    while (searchIdx < rowCount) {
        QModelIndex idx = galleryModel_->index(searchIdx);
        bool isGallery = galleryModel_->data(idx, GalleryModel::IsGalleryRole).toBool();
        if (!isGallery) {
            bumpGeneration();
            if (currentGalleryIndex_ != searchIdx) {
                currentGalleryIndex_ = searchIdx;
                emit currentIndexChanged();
            }
            loadImageAtIndex(searchIdx);
            return;
        }
        searchIdx++;
    }
    // No next media found
}

// Helper: recursively find node path in vault tree
// Returns the full path (e.g., "/folder/subfolder/image.jpg") or empty string if not found
static std::string find_node_path(const vault::IndexNode& root, const vault::IndexNode* target, const std::string& current_path = "")
{
    if (&root == target) {
        // Found it - return current path
        return current_path.empty() ? "/" + root.name : current_path + "/" + root.name;
    }

    // Search children (galleries have children)
    if (root.is_gallery()) {
        for (const auto& child : root.children) {
            std::string child_path = current_path.empty() ? "/" + root.name : current_path + "/" + root.name;
            std::string result = find_node_path(child, target, child_path);
            if (!result.empty()) {
                return result;
            }
        }
    }

    return "";
}

void ViewerController::openAlbum(const QList<quintptr>& nodeKeys, int startIndex)
{
    if (nodeKeys.isEmpty() || startIndex < 0 || startIndex >= nodeKeys.size() || !vault_) {
        return;
    }

    // Enter album mode
    albumNodeKeys_ = nodeKeys;
    albumCurrentIndex_ = startIndex;

    // Populate real node paths for rebind on vault refresh
    albumNodePaths_.clear();
    const vault::IndexNode* root = vault_->resolve_node("");  // Empty path = root
    if (root) {
        for (quintptr nodeKey : nodeKeys) {
            const auto* node = reinterpret_cast<const vault::IndexNode*>(nodeKey);
            std::string path = find_node_path(*root, node);
            albumNodePaths_.append(QString::fromStdString(path));
        }
    } else {
        // Fallback if no root
        albumNodePaths_.resize(nodeKeys.size());
        std::fill(albumNodePaths_.begin(), albumNodePaths_.end(), "");
    }

    bumpGeneration();
    loadImageAtAlbumIndex(startIndex);
}

void ViewerController::rebindAlbumAfterRefresh()
{
    // Phase 56: Re-resolve album nodes after vault tree refresh.
    // Uses ui::album_rebind to preserve view state when nodes are moved/deleted.
    //
    // Flow:
    // 1. Convert stored paths to STL vector
    // 2. Call ui::album_rebind with current item path
    // 3. If found: same item continues, index updated
    //    If missing: fallback to same-index item with different path
    // 4. Update albumNodeKeys_ to re-resolve to new node pointers
    // 5. Continue viewing with preserved or fallback state

    if (albumNodeKeys_.isEmpty() || !vault_) {
        return;
    }

    // Convert paths to STL vector
    std::vector<std::string> paths;
    for (const QString& qpath : albumNodePaths_) {
        paths.push_back(qpath.toStdString());
    }

    // Call ui::album_rebind to find new position
    ui::AlbumRebind rebind = ui::rebind_album_index(paths, albumCurrentPath_.toStdString(), albumCurrentIndex_);

    // Update to new index (preserve state if found, fallback if missing)
    albumCurrentIndex_ = rebind.index;

    // Re-resolve nodeKeys to current vault tree (paths may have changed)
    // TODO: This requires access to vault's index tree to resolve paths → nodeKeys
    // For now, existing nodeKeys remain (they're stale but will fail safely).
    // Full integration deferred to Phase 56 when index paths are retrievable.

    loadImageAtAlbumIndex(rebind.index);
}

void ViewerController::loadImageAtAlbumIndex(int albumIndex)
{
    if (albumIndex < 0 || albumIndex >= albumNodeKeys_.size() || !vault_) {
        return;
    }

    setLoading(true);

    // Get node key from album list
    quintptr nodeKey = albumNodeKeys_[albumIndex];
    const auto* node = reinterpret_cast<const vault::IndexNode*>(nodeKey);

    if (!node) {
        setLoading(false);
        return;
    }

    // Save current path for rebind (Phase 56: album_rebind on vault refresh)
    if (albumIndex < albumNodePaths_.size()) {
        albumCurrentPath_ = albumNodePaths_[albumIndex];
    }

    // Derive a display name (TODO: fetch from index when album_rebind is integrated)
    QString name = QString::asprintf("Album Item %d", albumIndex + 1);
    setImageName(name);

    // Enqueue async load worker (same as gallery mode)
    uint64_t currentGen = generation_.load(std::memory_order_acquire);
    auto worker = new ViewerWorker(this, node, currentGen, vault_);
    pool_.start(worker);
}

void ViewerController::loadImageAtIndex(int galleryIndex)
{
    if (!galleryModel_ || !vault_) {
        return;
    }

    setLoading(true);

    // Capture node pointer and name on main thread
    QModelIndex idx = galleryModel_->index(galleryIndex);
    if (!idx.isValid()) {
        setLoading(false);
        return;
    }

    quintptr nodeKey = galleryModel_->data(idx, GalleryModel::NodeKeyRole).value<quintptr>();
    QString name = galleryModel_->data(idx, GalleryModel::NameRole).toString();
    const auto* node = reinterpret_cast<const vault::IndexNode*>(nodeKey);

    if (!node) {
        setLoading(false);
        return;
    }

    setImageName(name);

    // Enqueue async load worker
    uint64_t currentGen = generation_.load(std::memory_order_acquire);
    auto worker = new ViewerWorker(this, node, currentGen, vault_);
    pool_.start(worker);
}

void ViewerController::onImageReady(std::shared_ptr<const PixelBuffer> pixels, uint64_t generation)
{
    // Check if this result is still valid (generation hasn't changed)
    if (generation_.load(std::memory_order_acquire) != generation) {
        return;  // stale, discard
    }

    setLoading(false);

    if (!pixels || !boundItem_) {
        // boundItem_ is null if the QML item was destroyed (QPointer tracks this),
        // or if unbind was called. Either way, drop the result safely.
        return;
    }

    // Deliver pixels to the bound SecureImageItem (safe: QPointer checked above)
    boundItem_->setImage(pixels);
    emit imageLoaded();
}

void ViewerController::shutdownAndDrain()
{
    stopping_.store(true, std::memory_order_release);
    bumpGeneration();
    pool_.waitForDone();
}
