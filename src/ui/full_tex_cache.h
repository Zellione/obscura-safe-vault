#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "image/image.h"

namespace vault { class Vault; struct IndexNode; }
namespace image { class DecodeWorker; }

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
    // With a `worker`, acquire() is non-blocking: the slow decode runs off-thread
    // and the texture appears on a later frame (pump() uploads finished decodes).
    // Without one (worker == nullptr, e.g. headless tests) acquire() decodes
    // synchronously as before.
    FullTexCache(vault::Vault& vault, SDL_Renderer* renderer,
                 image::DecodeWorker* worker = nullptr)
        : vault_(vault), renderer_(renderer), worker_(worker) {}
    ~FullTexCache() { evict_except({}); }

    FullTexCache(const FullTexCache&)            = delete;
    FullTexCache& operator=(const FullTexCache&) = delete;

    // Return `node`'s cached texture. On a miss: decode synchronously (no worker)
    // or enqueue an off-thread decode and return nullptr until it lands (worker).
    // Returns nullptr on failure, leaving a human-readable reason in error().
    FullTex* acquire(const vault::IndexNode& node);

    // Upload any finished off-thread decodes to GPU textures (call once per frame
    // before acquire()). Returns true if at least one texture became available.
    bool pump();

    // Destroy every cached texture whose key is not in `keep`, and drop any
    // not-yet-started decode jobs for the same keys.
    void evict_except(std::span<const uint64_t> keep);

    [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
    // Upload decoded RGB pixels to a cached GPU texture. Returns nullptr on
    // failure (leaving error_ set).
    FullTex* upload(uint64_t key, const image::ImageData& img);

    vault::Vault&                         vault_;
    SDL_Renderer*                         renderer_;
    image::DecodeWorker*                  worker_ = nullptr;
    std::unordered_map<uint64_t, FullTex> cache_;
    std::unordered_set<uint64_t>          failed_;   // decodes that gave up (corrupt)
    std::string                           error_;
};

} // namespace ui
