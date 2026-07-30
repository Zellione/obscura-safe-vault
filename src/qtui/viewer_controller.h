#pragma once

#include <QObject>
#include <QThreadPool>
#include <QPointer>
#include <memory>
#include <atomic>

#include "pixel_buffer.h"

namespace vault {
    class Vault;
    struct IndexNode;
}

class SecureImageItem;
class GalleryModel;

// Asynchronous image viewer: decodes full-resolution encrypted images from vault
// on QThreadPool workers, delivers pixels to SecureImageItem.
//
// CRITICAL LIFETIME SAFETY (same as ThumbCache):
// Workers capture const IndexNode* pointers to vault tree nodes.
// When lock() is called, the vault wipes the tree and workers may dereference freed nodes.
// Generation epoch (generation_) is bumped on every open()/next()/prev()/lock to invalidate stale workers.
//
// Threading: safe to call open/next/prev from any thread; imageLoaded() signal
// delivered on main thread via queued connection.
class ViewerController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString imageName READ imageName NOTIFY imageNameChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
public:
    explicit ViewerController(vault::Vault* vault, GalleryModel* galleryModel, QObject* parent = nullptr);
    ~ViewerController();

    // Bind the SecureImageItem that will receive decoded pixels (called from QML).
    Q_INVOKABLE void bindItem(SecureImageItem* item);

    // Open image at gallery row (must be valid image row, not gallery).
    Q_INVOKABLE void open(int galleryRow);

    // Navigate to previous media item (skip galleries).
    Q_INVOKABLE void prev();

    // Navigate to next media item (skip galleries).
    Q_INVOKABLE void next();

    // Drain in-flight workers before vault lock (call from main thread).
    void shutdownAndDrain();

    [[nodiscard]] QString imageName() const { return imageName_; }
    [[nodiscard]] bool loading() const { return loading_; }
    [[nodiscard]] int currentIndex() const { return currentGalleryIndex_; }

signals:
    void imageLoaded();
    void imageNameChanged();
    void loadingChanged();
    void currentIndexChanged();

private:
    friend class ViewerWorker;

    // Called from worker via queued connection to deliver pixels.
    void onImageReady(std::shared_ptr<const PixelBuffer> pixels, uint64_t generation);

    void setLoading(bool state);
    void setImageName(const QString& name);
    void bumpGeneration();
    void loadImageAtIndex(int galleryIndex);

    vault::Vault* vault_;
    GalleryModel* galleryModel_;
    QPointer<SecureImageItem> boundItem_;  // QPointer nulls automatically if destroyed

    // Generation epoch: bumped on every open/next/prev/lock/unbind to invalidate stale workers
    std::atomic<uint64_t> generation_{0};
    // Stopping flag: set by shutdownAndDrain
    std::atomic<bool> stopping_{false};

    QString imageName_;
    bool loading_ = false;

    int currentGalleryIndex_ = -1;  // current image row in gallery

    // Dedicated thread pool for full-image loading
    QThreadPool pool_;
};
