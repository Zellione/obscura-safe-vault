#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <unordered_set>

#include "ui/gallery_cover.h"   // ui::CoverSpan

namespace gfx { class Renderer; class FontAtlas; class TextureCache; }
namespace image { class DecodeWorker; }
namespace vault { class Vault; struct IndexNode; }

namespace ui {

// The shared dependencies a tile-thumbnail draw needs: the vault to decrypt
// thumb/poster chunks from, the GPU texture cache they upload into, the
// off-thread decode worker that turns decrypted bytes into pixels, and the
// per-screen set of chunk keys whose decode gave up. Bundled so the draw/texture
// helpers below take a single context argument (and to keep host screens within
// the SonarCloud method/field budgets, cpp:S1448/S1820).
//
// Each host screen owns its own worker + failed-set and pumps finished decodes
// into the cache itself (see GalleryGrid::pump_thumbs); these helpers only
// *request* a decode (returning nullptr until it lands) and draw whatever the
// cache already has. No new disk path — decryption flows through the existing
// SecureBytes -> worker pipeline.
struct ThumbContext {
    const vault::Vault&           vault;
    gfx::TextureCache&            cache;
    image::DecodeWorker&          worker;
    std::unordered_set<uint64_t>& failed;
};

// A stable per-node cache key for its thumbnail texture, and whether one
// exists at all. A video's thumbnail is its poster frame (vmeta.poster_*) —
// the image thumbnail fields (meta.thumb_*) are always zero on a video node,
// so gating on those alone always reports "no thumbnail" for every video.
struct ThumbKey {
    uint64_t key;
    bool     present;
};

[[nodiscard]] ThumbKey thumb_key_for(const vault::IndexNode& node);

// The GPU texture for a media node's own thumbnail, or nullptr while it is
// pending/failed (in which case a decode is submitted as a side effect).
[[nodiscard]] SDL_Texture* tile_thumb_texture(const ThumbContext& ctx,
                                              const vault::IndexNode& node);

// The GPU texture for one cover chunk (a gallery's representative image/poster,
// possibly a descendant's), keyed/read by raw chunk span. Same pending/failed
// semantics as tile_thumb_texture.
[[nodiscard]] SDL_Texture* tile_cover_tex(const ThumbContext& ctx,
                                          const CoverSpan& span);

// Draw a node as a tile thumbnail inside `box`:
//   * gallery  -> a gold folder behind a representative cover or a 2x2 montage
//                 of up to four sub-gallery covers (Phase 19); bare folder if
//                 the subtree is empty.
//   * image    -> the thumbnail, aspect-fit on black ("(no thumb)" placeholder
//                 while it decodes).
//   * video    -> the poster plus a centred play-triangle badge.
void draw_tile_thumb(gfx::Renderer& r, gfx::FontAtlas& font,
                     const ThumbContext& ctx, const vault::IndexNode& n,
                     const SDL_FRect& box);

} // namespace ui
