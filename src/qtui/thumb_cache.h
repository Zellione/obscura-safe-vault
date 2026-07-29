#pragma once

#include <QObject>
#include <QThreadPool>
#include <memory>
#include <map>
#include <set>
#include <mutex>

#include "pixel_buffer.h"

namespace vault {
    class Vault;
}

// Asynchronous thumbnail cache: decodes encrypted thumbnails from vault
// on QThreadPool workers, signals ready(key) when pixels are available.
// LRU-bounded to 256 entries; cleared on vault lock.
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
    [[nodiscard]] std::shared_ptr<const PixelBuffer> pixels(quintptr key) const;

    // Clear all cached entries (called on vault lock).
    void clearAll();

    // Set vault reference for thumbnail reading (call from main thread).
    void setVault(vault::Vault* vault) noexcept;

signals:
    // Emitted on main thread when pixels(key) becomes ready.
    void ready(quintptr key);

private:
    friend class ThumbCacheWorker;

    // Called from worker via queued connection to store result.
    void onPixelsReady(quintptr key, std::shared_ptr<const PixelBuffer> pixels);

    static constexpr size_t MAX_LRU_ENTRIES = 256;

    vault::Vault* vault_ = nullptr;

    mutable std::mutex cacheMutex_;
    // LRU: key → pixels; order tracked by access_order_
    std::map<quintptr, std::shared_ptr<const PixelBuffer>> cache_;
    // LRU age: quintptr key, insertion order
    std::vector<quintptr> lruOrder_;

    // In-flight requests (to avoid duplicate work)
    std::set<quintptr> inFlight_;

    QThreadPool pool_;
};
