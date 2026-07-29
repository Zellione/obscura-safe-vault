#include "thumb_cache.h"

#include <QMetaObject>
#include <QRunnable>
#include <QtCore/qloggingcategory.h>
#include <algorithm>

#include "vault/vault.h"
#include "image/decode.h"

Q_LOGGING_CATEGORY(lcThumbCache, "osv.thumb_cache")

// Worker runnable: decode a thumbnail off-thread.
// Captures const IndexNode*, vault pointer, and generation epoch from main thread.
// CRITICAL: Checks generation before read_thumbnail to guard against vault lifetime issues.
class ThumbCacheWorker : public QRunnable {
public:
    ThumbCacheWorker(ThumbCache* cache, quintptr key, const vault::IndexNode* node,
                     vault::Vault* vault, uint64_t generation)
        : cache_(cache), key_(key), node_(node), vault_(vault), generation_(generation)
    {
    }

    void run() override
    {
        if (!vault_ || !node_) {
            return;
        }

        try {
            // Check generation immediately before accessing vault.
            // If generation has changed, this worker is stale and must not use vault_.
            // This guard prevents use-after-free if lock() was called and tree was freed.
            if (cache_->generation_.load(std::memory_order_acquire) != generation_) {
                qCDebug(lcThumbCache) << "Stale worker generation, aborting";
                return;
            }

            // Decode thumbnail from vault (thread-safe: uses thumb_fp_ + thumb_mutex_)
            crypto::SecureBytes secure_bytes;
            auto result = vault_->read_thumbnail(*node_, secure_bytes);

            if (result != vault::VaultResult::Ok) {
                qCDebug(lcThumbCache) << "Failed to read thumbnail for node";
                return;
            }

            // Decode from encrypted bytes to ImageData (auto-detects JPEG format)
            const std::span<const uint8_t> span(secure_bytes.data(), secure_bytes.size());
            auto img_data = image::decode_from_memory(span);
            // secure_bytes goes out of scope here and is wiped

            if (!img_data.has_value()) {
                qCDebug(lcThumbCache) << "Failed to decode thumbnail image";
                return;
            }

            // Expand RGB to RGBA
            auto pixels = std::make_shared<PixelBuffer>(expand_rgb_to_rgba(*img_data));

            // Post result back to main thread via queued connection
            auto cache_ptr = cache_;
            auto key_val = key_;
            auto pixels_val = pixels;
            auto gen_val = generation_;
            QMetaObject::invokeMethod(
                cache_ptr,
                [cache_ptr, key_val, pixels_val, gen_val]() {
                    cache_ptr->onPixelsReady(key_val, pixels_val, gen_val);
                },
                Qt::QueuedConnection
            );
        } catch (const std::exception& e) {
            qCWarning(lcThumbCache) << "Exception in thumbnail worker:" << e.what();
        }
    }

private:
    ThumbCache* cache_;
    quintptr key_;
    const vault::IndexNode* node_;
    vault::Vault* vault_;
    uint64_t generation_;  // Snapshot of generation at enqueue time
};

// Static singleton
static ThumbCache* g_instance = nullptr;

ThumbCache* ThumbCache::instance() noexcept
{
    return g_instance;
}

ThumbCache::ThumbCache(QObject* parent)
    : QObject(parent)
{
    g_instance = this;
    // Register metatype for signal/slot delivery
    qRegisterMetaType<quintptr>("quintptr");
}

void ThumbCache::setVault(vault::Vault* vault) noexcept
{
    vault_ = vault;
}

void ThumbCache::request(quintptr key)
{
    // Bail if stopping or no vault
    if (stopping_.load(std::memory_order_acquire) || !vault_) {
        qCDebug(lcThumbCache) << "request() noop: stopping=" << stopping_.load() << "vault=" << (vault_ ? "set" : "null");
        return;
    }
    qCDebug(lcThumbCache) << "request() for key=" << key;

    uint64_t current_gen;
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);

        // Already cached?
        if (cache_.count(key) > 0) {
            emit ready(key);
            return;
        }

        // Already in-flight?
        if (inFlight_.count(key) > 0) {
            return;
        }

        // Mark as in-flight
        inFlight_.insert(key);

        // Snapshot generation for this worker
        current_gen = generation_.load(std::memory_order_acquire);
    }

    // Reconstruct const IndexNode* from quintptr key
    const auto* node = reinterpret_cast<const vault::IndexNode*>(key);

    // Create worker with generation snapshot and queue on dedicated pool
    auto* worker = new ThumbCacheWorker(this, key, node, vault_, current_gen);
    worker->setAutoDelete(true);
    pool_.start(worker);
}

std::shared_ptr<const PixelBuffer> ThumbCache::pixels(quintptr key)
{
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        // True LRU: move accessed key to end of lruOrder
        auto order_it = std::find(lruOrder_.begin(), lruOrder_.end(), key);
        if (order_it != lruOrder_.end()) {
            lruOrder_.erase(order_it);
            lruOrder_.push_back(key);
        }
        return it->second;
    }
    return nullptr;
}

void ThumbCache::drainPending()
{
    // Wait for in-flight workers to complete without stopping the cache.
    // Safe to call before GalleryModel::refresh to prevent node pointer staleness.
    pool_.waitForDone();
}

void ThumbCache::shutdownAndDrain()
{
    // CRITICAL: This must be called from main thread before Vault::lock().
    // Drain-before-lock ordering is load-bearing for lifetime safety.

    // Signal workers to stop (they check this before accessing vault)
    stopping_.store(true, std::memory_order_release);

    // Bump generation so in-flight workers ignore their results
    generation_.fetch_add(1, std::memory_order_release);

    // Wait for all in-flight workers to complete
    pool_.waitForDone();
}

void ThumbCache::clearAll()
{
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.clear();
        inFlight_.clear();
        lruOrder_.clear();
        deliveredCount_ = 0;  // Reset test counter
        vault_ = nullptr;  // Invalidate vault pointer
    }

    // Allow new requests after clear (e.g., after unlock with new vault)
    stopping_.store(false, std::memory_order_release);
}

void ThumbCache::onPixelsReady(quintptr key, std::shared_ptr<const PixelBuffer> pixels, uint64_t generation)
{
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);

        // Drop results from stale generations (worker ran after lock/clearAll)
        if (generation != generation_.load(std::memory_order_acquire)) {
            inFlight_.erase(key);
            qCDebug(lcThumbCache) << "Dropped result from stale generation";
            return;
        }

        // Mark as no longer in-flight
        inFlight_.erase(key);

        // Add to cache (not already there due to inFlight dedup)
        cache_[key] = pixels;
        lruOrder_.push_back(key);
        deliveredCount_++;  // Test-only: track successful deliveries

        // Evict oldest LRU entry if over limit
        while (cache_.size() > MAX_LRU_ENTRIES && !lruOrder_.empty()) {
            quintptr oldest = lruOrder_.front();
            lruOrder_.erase(lruOrder_.begin());
            cache_.erase(oldest);
        }
    }

    // Signal on main thread (already here via queued connection)
    emit ready(key);
}
