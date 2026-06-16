#include "ui/full_tex_cache.h"

#include <algorithm>

#include "crypto/secure_mem.h"
#include "image/decode.h"
#include "image/decode_worker.h"
#include "vault/index.h"
#include "vault/vault.h"

namespace ui {

FullTex* FullTexCache::upload(uint64_t key, const image::ImageData& img)
{
    SDL_Texture* tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24,
                                         SDL_TEXTUREACCESS_STATIC, img.width, img.height);
    if (!tex) { error_ = "Could not upload image."; return nullptr; }
    // Enable alpha blending so the slideshow cross-fade's per-draw alpha takes
    // effect (a no-op for the opaque Fit/FillScroll draws).
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    if (!SDL_UpdateTexture(tex, nullptr, img.pixels.data(), img.width * 3)) {
        SDL_DestroyTexture(tex);
        error_ = "Could not upload image.";
        return nullptr;
    }
    auto [it, _] = cache_.try_emplace(key, FullTex{tex,
                                                   static_cast<float>(img.width),
                                                   static_cast<float>(img.height)});
    return &it->second;
}

FullTex* FullTexCache::acquire(const vault::IndexNode& node)
{
    const uint64_t key = node.meta.data_offset;
    if (auto it = cache_.find(key); it != cache_.end()) return &it->second;

    // Read + decrypt the stored (still-compressed) bytes on this thread — fast,
    // and the only place that touches the vault's file handle. SecureBytes keeps
    // the plaintext mlock'd and wipes it when it goes out of scope / after decode.
    crypto::SecureBytes sb;
    if (worker_) {
        // Async: a corrupt image that already failed to decode is not retried;
        // an in-flight decode is left to land via pump().
        if (failed_.contains(key) || worker_->pending(key)) return nullptr;
        if (vault_.read_image(node, sb) != vault::VaultResult::Ok) {
            error_ = "Could not decrypt image.";
            return nullptr;
        }
        worker_->submit(key, std::move(sb));
        return nullptr;   // not ready this frame — caller draws a placeholder
    }

    // Synchronous fallback (no worker, e.g. headless tests).
    if (vault_.read_image(node, sb) != vault::VaultResult::Ok) {
        error_ = "Could not decrypt image.";
        return nullptr;
    }
    auto img = image::decode_from_memory(sb.as_span());
    if (!img) { error_ = "Could not decode image."; return nullptr; }
    // SecureBytes `sb` wipes the decrypted plaintext on scope exit; the pixels
    // now live only in the GPU texture.
    return upload(key, *img);
}

bool FullTexCache::pump()
{
    if (!worker_) return false;
    bool any = false;
    while (auto res = worker_->take_result()) {
        if (res->image) {
            if (upload(res->key, *res->image)) any = true;
        } else {
            failed_.insert(res->key);   // corrupt/unsupported — stop retrying it
        }
    }
    return any;
}

void FullTexCache::evict_except(std::span<const uint64_t> keep)
{
    std::erase_if(cache_, [keep](const auto& entry) {
        const auto& [key, ft] = entry;
        if (std::ranges::find(keep, key) != keep.end()) return false;
        SDL_DestroyTexture(ft.tex);
        return true;
    });
    if (worker_) worker_->retain(keep);   // drop queued decodes for evicted images
}

} // namespace ui
