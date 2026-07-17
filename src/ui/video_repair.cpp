#include "ui/video_repair.h"

#include <string>

#include "vault/index.h"
#include "vault/vault.h"

namespace ui {

void repair_unknown_video_metadata(vault::Vault& vault, std::string_view gallery_path,
                                    std::span<const vault::IndexNode* const> children)
{
    for (const vault::IndexNode* n : children) {
        if (!n->is_video() || n->vmeta.codec != vault::VideoCodec::Unknown) continue;
        // Built imperatively (reserve + append), not via chained operator+
        // concatenation: GCC 16 -O3 -Wrestrict raises a false-positive
        // "__builtin_memcpy... overlaps" error on the equivalent
        // `std::string(gallery_path) + "/" + n->name` form on this project's
        // std/GCC version, even though gallery_path/n->name never alias.
        std::string path;
        path.reserve(gallery_path.size() + 1 + n->name.size());
        if (!gallery_path.empty()) {
            path.append(gallery_path);
            path.push_back('/');
        }
        path.append(n->name);
        (void)vault.repair_video_metadata(path);
    }
}

}  // namespace ui
