#include "ui/album_rebind.h"

#include <algorithm>

namespace ui {

AlbumRebind rebind_album_index(const std::vector<std::string>& new_paths,
                               std::string_view current_path, int current_index)
{
    if (new_paths.empty()) return {.index = 0, .preserve = false};

    if (const auto it = std::ranges::find(new_paths, current_path); it != new_paths.end())
        return {.index = static_cast<int>(std::distance(new_paths.begin(), it)),
                .preserve = true};

    return {.index = std::clamp(current_index, 0, static_cast<int>(new_paths.size()) - 1),
            .preserve = false};
}

size_t compact_album(std::vector<const vault::IndexNode*>& images,
                     std::vector<std::string>&             paths)
{
    const size_t n = std::min(images.size(), paths.size());
    size_t w = 0;
    for (size_t r = 0; r < n; ++r) {
        if (images[r] == nullptr) continue;
        if (w != r) {
            images[w] = images[r];
            paths[w]  = std::move(paths[r]);
        }
        ++w;
    }
    const size_t removed = n - w;
    images.resize(w);
    paths.resize(w);
    return removed;
}

int media_index_in_listing(std::span<const vault::IndexNode* const> listing, int listing_index)
{
    const auto n = static_cast<int>(listing.size());
    const int idx = std::clamp(listing_index, 0, std::max(0, n - 1));
    int media_before = 0;
    for (int i = 0; i < idx; ++i) {
        const vault::IndexNode* node = listing[static_cast<size_t>(i)];
        if (node != nullptr && node->is_media()) ++media_before;
    }
    return media_before;
}

int listing_index_of_media(std::span<const vault::IndexNode* const> listing, int media_index)
{
    int media_count = 0;
    for (const vault::IndexNode* node : listing)
        if (node != nullptr && node->is_media()) ++media_count;
    if (media_count == 0) return 0;

    const int want = std::clamp(media_index, 0, media_count - 1);
    int seen = 0;
    for (int i = 0; i < static_cast<int>(listing.size()); ++i) {
        if (const vault::IndexNode* node = listing[static_cast<size_t>(i)];
            node == nullptr || !node->is_media())
            continue;
        if (seen == want) return i;
        ++seen;
    }
    return 0;  // unreachable: `want` is within the counted media range
}

} // namespace ui
