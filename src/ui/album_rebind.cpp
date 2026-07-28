#include "ui/album_rebind.h"

#include <algorithm>

namespace ui {

AlbumRebind rebind_album_index(const std::vector<std::string>& new_paths,
                               std::string_view current_path, int current_index)
{
    if (new_paths.empty()) return {.index = 0, .preserve = false};

    const auto it = std::ranges::find(new_paths, current_path);
    if (it != new_paths.end())
        return {.index = static_cast<int>(std::distance(new_paths.begin(), it)),
                .preserve = true};

    return {.index = std::clamp(current_index, 0, static_cast<int>(new_paths.size()) - 1),
            .preserve = false};
}

} // namespace ui
