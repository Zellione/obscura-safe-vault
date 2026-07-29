#include "thumb_cache.h"

#include <QMetaObject>
#include <QRunnable>
#include <QtCore/qloggingcategory.h>

#include "vault/vault.h"
#include "image/decode.h"

Q_LOGGING_CATEGORY(lcThumbCache, "osv.thumb_cache")

// Worker runnable: decode a thumbnail off-thread.
// Captures const IndexNode* and vault pointer from main thread.
class ThumbCacheWorker : public QRunnable {
public:
    ThumbCacheWorker(ThumbCache* cache, quintptr key, const vault::IndexNode* node, vault::Vault* vault)
        : cache_(cache), key_(key), node_(node), vault_(vault)
    {
    }

    void run() override
    {
        if (!vault_ || !node_) {
            return;
        }

        try {
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
            auto cache_ptr = cache_;  // capture by value (copy pointer)
            auto key_val = key_;
            auto pixels_val = pixels;
            QMetaObject::invokeMethod(
                cache_ptr,
                [cache_ptr, key_val, pixels_val]() {
                    cache_ptr->onPixelsReady(key_val, pixels_val);
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
    if (!vault_) {
        return;
    }

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
    }

    // Reconstruct const IndexNode* from quintptr key
    const auto* node = reinterpret_cast<const vault::IndexNode*>(key);

    // Create worker and queue on pool
    auto* worker = new ThumbCacheWorker(this, key, node, vault_);
    worker->setAutoDelete(true);
    pool_.start(worker);
}

std::shared_ptr<const PixelBuffer> ThumbCache::pixels(quintptr key) const
{
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return it->second;
    }
    return nullptr;
}

void ThumbCache::clearAll()
{
    std::lock_guard<std::mutex> lock(cacheMutex_);
    cache_.clear();
    inFlight_.clear();
    lruOrder_.clear();
}

void ThumbCache::onPixelsReady(quintptr key, std::shared_ptr<const PixelBuffer> pixels)
{
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);

        // Mark as no longer in-flight
        inFlight_.erase(key);

        // Add to cache
        cache_[key] = pixels;
        lruOrder_.push_back(key);

        // Evict oldest if over limit
        while (cache_.size() > MAX_LRU_ENTRIES && !lruOrder_.empty()) {
            quintptr oldest = lruOrder_.front();
            lruOrder_.erase(lruOrder_.begin());
            cache_.erase(oldest);
        }
    }

    // Signal on main thread (already here via queued connection)
    emit ready(key);
}
