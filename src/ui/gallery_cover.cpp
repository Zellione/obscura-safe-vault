#include "ui/gallery_cover.h"

#include "vault/vault.h"   // vault::media_thumb_chunk_ref

namespace ui {

namespace {
// The cover chunk reference stored directly on a media node, or nullopt if it
// has no thumbnail/poster (length 0).
std::optional<CoverSpan> media_span(const vault::IndexNode& n) noexcept
{
    const vault::ChunkRef r = vault::media_thumb_chunk_ref(n);
    if (r.length == 0) return std::nullopt;
    return CoverSpan{r};
}
}  // namespace

std::optional<CoverSpan> resolve_single_cover(const vault::IndexNode& gallery,
                                              uint32_t max_depth)
{
    if (max_depth == 0) return std::nullopt;
    for (const auto& child : gallery.children) {
        if (auto span = media_span(child)) return span;
        if (child.is_gallery())
            if (auto span = resolve_single_cover(child, max_depth - 1)) return span;
    }
    return std::nullopt;
}

std::vector<CoverSpan> resolve_covers(const vault::IndexNode& gallery,
                                      uint32_t max_depth)
{
    if (max_depth == 0) return {};

    // Non-leaf (holds sub-galleries) -> montage of up to 4 sub-gallery covers.
    std::vector<CoverSpan> out;
    for (const auto& child : gallery.children) {
        if (!child.is_gallery()) continue;
        if (auto span = resolve_single_cover(child, max_depth - 1)) {
            out.push_back(*span);
            if (out.size() == 4) return out;
        }
    }
    if (!out.empty()) return out;

    // Leaf (holds media, or empty) -> the single first-media cover, if any.
    if (auto span = resolve_single_cover(gallery, max_depth)) out.push_back(*span);
    return out;
}

}  // namespace ui
