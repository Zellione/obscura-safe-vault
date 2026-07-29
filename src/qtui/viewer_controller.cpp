#include "viewer_controller.h"

#include <QRunnable>
#include <QThreadPool>

#include "image/decode.h"
#include "secure_image_item.h"
#include "gallery_model.h"
#include "vault/vault.h"

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
    currentGalleryIndex_ = galleryRow;
    loadImageAtIndex(galleryRow);
}

void ViewerController::prev()
{
    if (!galleryModel_ || currentGalleryIndex_ < 0) {
        return;
    }

    // Find previous media item (skip galleries)
    int searchIdx = currentGalleryIndex_ - 1;
    while (searchIdx >= 0) {
        // Check if row at searchIdx is a media item (not gallery)
        QModelIndex idx = galleryModel_->index(searchIdx);
        bool isGallery = galleryModel_->data(idx, GalleryModel::IsGalleryRole).toBool();
        if (!isGallery) {
            bumpGeneration();
            currentGalleryIndex_ = searchIdx;
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

    int rowCount = galleryModel_->rowCount();
    int searchIdx = currentGalleryIndex_ + 1;
    while (searchIdx < rowCount) {
        QModelIndex idx = galleryModel_->index(searchIdx);
        bool isGallery = galleryModel_->data(idx, GalleryModel::IsGalleryRole).toBool();
        if (!isGallery) {
            bumpGeneration();
            currentGalleryIndex_ = searchIdx;
            loadImageAtIndex(searchIdx);
            return;
        }
        searchIdx++;
    }
    // No next media found
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
