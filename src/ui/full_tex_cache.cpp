#include "ui/full_tex_cache.h"

#include <algorithm>

#include "crypto/secure_mem.h"
#include "image/decode.h"
#include "vault/index.h"
#include "vault/vault.h"

namespace ui {

FullTex* FullTexCache::acquire(const vault::IndexNode& node)
{
    const uint64_t key = node.meta.data_offset;
    if (auto it = cache_.find(key); it != cache_.end()) return &it->second;

    crypto::SecureBytes sb;
    if (vault_.read_image(node, sb) != vault::VaultResult::Ok) {
        error_ = "Could not decrypt image.";
        return nullptr;
    }
    auto img = image::decode_from_memory(sb.as_span());
    if (!img) { error_ = "Could not decode image."; return nullptr; }

    SDL_Texture* tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24,
                                         SDL_TEXTUREACCESS_STATIC, img->width, img->height);
    if (!tex) { error_ = "Could not upload image."; return nullptr; }
    // Enable alpha blending so the slideshow cross-fade's per-draw alpha takes
    // effect (a no-op for the opaque Fit/FillScroll draws).
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    if (!SDL_UpdateTexture(tex, nullptr, img->pixels.data(), img->width * 3)) {
        SDL_DestroyTexture(tex);
        error_ = "Could not upload image.";
        return nullptr;
    }
    // SecureBytes `sb` wipes the decrypted plaintext on scope exit; the pixels
    // now live only in the GPU texture.
    auto [it, _] = cache_.try_emplace(key, FullTex{tex,
                                                   static_cast<float>(img->width),
                                                   static_cast<float>(img->height)});
    return &it->second;
}

void FullTexCache::evict_except(std::span<const uint64_t> keep)
{
    std::erase_if(cache_, [keep](const auto& entry) {
        const auto& [key, ft] = entry;
        if (std::ranges::find(keep, key) != keep.end()) return false;
        SDL_DestroyTexture(ft.tex);
        return true;
    });
}

} // namespace ui
