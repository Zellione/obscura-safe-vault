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

} // namespace ui
