#include "gfx/texture_cache.h"

namespace gfx {

TextureCache::TextureCache(SDL_Renderer* renderer, std::size_t budget_bytes)
    : renderer_(renderer), budget_(budget_bytes)
{
}

TextureCache::~TextureCache()
{
    clear();
}

void TextureCache::clear()
{
    for (auto& e : lru_)
        if (e.tex) SDL_DestroyTexture(e.tex);
    lru_.clear();
    map_.clear();
    total_bytes_ = 0;
}

void TextureCache::touch(std::list<Entry>::iterator it)
{
    // Move the entry to the front (most-recently-used).
    lru_.splice(lru_.begin(), lru_, it);
}

void TextureCache::evict_for(std::size_t incoming)
{
    // Drop least-recently-used entries (from the back) until the new entry fits,
    // or nothing is left to evict (a single oversized entry is allowed through).
    while (!lru_.empty() && total_bytes_ + incoming > budget_) {
        Entry& victim = lru_.back();
        if (victim.tex) SDL_DestroyTexture(victim.tex);
        total_bytes_ -= victim.bytes;
        map_.erase(victim.key);
        lru_.pop_back();
    }
}

SDL_Texture* TextureCache::get(uint64_t key)
{
    auto found = map_.find(key);
    if (found == map_.end()) return nullptr;
    touch(found->second);
    return found->second->tex;
}

bool TextureCache::contains(uint64_t key) const noexcept
{
    return map_.contains(key);
}

SDL_Texture* TextureCache::get_or_upload(uint64_t key, const image::ImageData& img)
{
    if (auto* cached = get(key)) return cached;

    if (img.width <= 0 || img.height <= 0 ||
        img.pixels.size() < static_cast<size_t>(img.width) * img.height * 3)
        return nullptr;

    SDL_Texture* tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24,
                                         SDL_TEXTUREACCESS_STATIC,
                                         img.width, img.height);
    if (!tex) return nullptr;
    if (!SDL_UpdateTexture(tex, nullptr, img.pixels.data(), img.width * 3)) {
        SDL_DestroyTexture(tex);
        return nullptr;
    }

    // Account for the on-GPU footprint as RGBA (4 bytes/pixel).
    const std::size_t bytes = static_cast<std::size_t>(img.width) * img.height * 4;
    evict_for(bytes);

    lru_.push_front(Entry{.key = key, .tex = tex, .bytes = bytes});
    map_[key] = lru_.begin();
    total_bytes_ += bytes;
    return tex;
}

} // namespace gfx
