#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>

namespace vault { class Vault; struct IndexNode; }

namespace ui {

// A decoded full-resolution texture and its natural pixel size.
struct FullTex {
    SDL_Texture* tex = nullptr;
    float        w   = 0.0f;
    float        h   = 0.0f;
};

// Bounded cache of decoded full-resolution image textures, keyed by a node's
// chunk `data_offset`. Decryption happens into a transient mlock'd SecureBytes
// that is wiped as soon as the pixels are uploaded to the GPU (invariant #1: no
// plaintext to disk). Shared by the image viewer's Fit/FillScroll rendering and
// the slideshow so a single large decode can't evict the gallery thumbnails and
// the decode/upload path lives in exactly one place.
class FullTexCache {
public:
    FullTexCache(vault::Vault& vault, SDL_Renderer* renderer)
        : vault_(vault), renderer_(renderer) {}
    ~FullTexCache() { evict_except({}); }

    FullTexCache(const FullTexCache&)            = delete;
    FullTexCache& operator=(const FullTexCache&) = delete;

    // Decode + upload `node`'s image (or return the cached texture). Returns
    // nullptr on failure, leaving a human-readable reason in error().
    FullTex* acquire(const vault::IndexNode& node);

    // Destroy every cached texture whose key is not in `keep`.
    void evict_except(std::span<const uint64_t> keep);

    [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
    vault::Vault&                         vault_;
    SDL_Renderer*                         renderer_;
    std::unordered_map<uint64_t, FullTex> cache_;
    std::string                           error_;
};

} // namespace ui
