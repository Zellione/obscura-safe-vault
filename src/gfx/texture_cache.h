#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>

#include "image/image.h"

namespace gfx {

/// LRU cache of GPU textures keyed by a caller-supplied id (e.g. an image node
/// id). When the total estimated GPU memory exceeds the budget, the
/// least-recently-used textures are evicted and destroyed.
///
/// The cache does not own the SDL_Renderer; the renderer must outlive the cache.
class TextureCache {
public:
    static constexpr std::size_t DEFAULT_BUDGET = std::size_t{256} * 1024 * 1024;

    explicit TextureCache(SDL_Renderer* renderer,
                          std::size_t budget_bytes = DEFAULT_BUDGET);
    ~TextureCache();

    TextureCache(const TextureCache&)            = delete;
    TextureCache& operator=(const TextureCache&) = delete;
    TextureCache(TextureCache&&)                 = delete;
    TextureCache& operator=(TextureCache&&)      = delete;

    /// Return the cached texture for `key`, uploading `img` first if absent.
    /// Marks the entry most-recently-used. Returns nullptr on upload failure.
    SDL_Texture* get_or_upload(uint64_t key, const image::ImageData& img);

    /// Return the cached texture for `key`, or nullptr if absent. Marks MRU.
    SDL_Texture* get(uint64_t key);

    [[nodiscard]] bool        contains(uint64_t key) const noexcept;
    [[nodiscard]] std::size_t count()  const noexcept { return map_.size(); }
    [[nodiscard]] std::size_t bytes()  const noexcept { return total_bytes_; }
    [[nodiscard]] std::size_t budget() const noexcept { return budget_; }

    /// Destroy every cached texture.
    void clear();

private:
    struct Entry {
        uint64_t     key   = 0;
        SDL_Texture* tex   = nullptr;
        std::size_t  bytes = 0;
    };

    // Evict from the LRU tail until `incoming` more bytes would fit the budget.
    void evict_for(std::size_t incoming);
    void touch(std::list<Entry>::iterator it); // move to MRU (front)

    SDL_Renderer* renderer_   = nullptr;
    std::size_t   budget_     = 0;
    std::size_t   total_bytes_ = 0;

    // front = most-recently-used, back = least-recently-used.
    std::list<Entry>                                           lru_;
    std::unordered_map<uint64_t, std::list<Entry>::iterator>   map_;
};

} // namespace gfx
