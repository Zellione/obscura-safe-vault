#pragma once

#include <QObject>
#include <QThreadPool>
#include <memory>
#include <map>
#include <set>
#include <mutex>
#include <atomic>

#include "pixel_buffer.h"

namespace vault {
    class Vault;
}

// Asynchronous thumbnail cache: decodes encrypted thumbnails from vault
// on QThreadPool workers, signals ready(key) when pixels are available.
// LRU-bounded to 256 entries; cleared on vault lock.
//
// CRITICAL LIFETIME SAFETY:
// Workers capture vault::Vault* and const IndexNode* pointers. When lock() is called,
// the vault wipes the in-memory index tree and workers may dereference freed nodes.
// To prevent use-after-free:
// 1. Generation epoch (generation_) is bumped on shutdownAndDrain()
// 2. Workers snapshot generation at enqueue and re-check immediately before read_thumbnail
// 3. Results for stale generations are dropped via onPixelsReady() check
// 4. Dedicated QThreadPool enables waitForDone before Vault::lock() runs
// 5. UnlockController::lock() calls shutdownAndDrain() BEFORE vault_.lock()
//    (drain ordering is load-bearing; comment on lock() invocation)
//
// Threading: safe to call request() from any thread; ready() signal
// delivered on main thread via queued connection.
class ThumbCache : public QObject {
    Q_OBJECT
public:
    explicit ThumbCache(QObject* parent = nullptr);

    // Get singleton instance (valid after app startup)
    static ThumbCache* instance() noexcept;

    // Request thumbnail decode for node at quintptr key.
    // No-op if already cached or in-flight.
    void request(quintptr key);

    // Retrieve cached pixels (or nullptr if not ready/miss).
    // Updates LRU order (true LRU, not FIFO).
    [[nodiscard]] std::shared_ptr<const PixelBuffer> pixels(quintptr key);

    // Test-only: count of thumbnails successfully delivered to cache.
    [[nodiscard]] int deliveredCount() const {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        return deliveredCount_;
    }

    // Wait for in-flight workers to complete (for model refresh before tree rebuild).
    // Does NOT stop accepting new requests (use before GalleryModel::refresh).
    void drainPending();

    // Stop accepting new requests and wait for in-flight workers to complete.
    // Sets stopping flag and bumps generation to invalidate results.
    // MUST be called from main thread before Vault::lock().
    void shutdownAndDrain();

    // Called after shutdownAndDrain when cache is being cleared (e.g., on lock or model refresh).
    // Clears cache, sets vault_ to nullptr.
    void clearAll();

    // Set vault reference for thumbnail reading (call from main thread).
    void setVault(vault::Vault* vault) noexcept;

signals:
    // Emitted on main thread when pixels(key) becomes ready.
    void ready(quintptr key);

private:
    friend class ThumbCacheWorker;

    // Called from worker via queued connection to store result.
    void onPixelsReady(quintptr key, std::shared_ptr<const PixelBuffer> pixels, uint64_t generation);

    static constexpr size_t MAX_LRU_ENTRIES = 256;

    vault::Vault* vault_ = nullptr;

    // Generation epoch: bumped on shutdownAndDrain to invalidate in-flight workers
    std::atomic<uint64_t> generation_{0};
    // Stopping flag: set by shutdownAndDrain, prevents new requests
    std::atomic<bool> stopping_{false};

    mutable std::mutex cacheMutex_;
    // LRU: key → pixels
    std::map<quintptr, std::shared_ptr<const PixelBuffer>> cache_;
    // LRU order: keys in insertion/access order (newest at end)
    std::vector<quintptr> lruOrder_;

    // In-flight requests (to avoid duplicate work)
    std::set<quintptr> inFlight_;

    // Test-only: count of thumbnails successfully delivered
    int deliveredCount_ = 0;

    // Dedicated thread pool for this cache (enables waitForDone before lock)
    QThreadPool pool_;
};
