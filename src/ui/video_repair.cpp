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
        const std::string path = gallery_path.empty()
                                      ? n->name
                                      : std::string(gallery_path) + "/" + n->name;
        (void)vault.repair_video_metadata(path);
    }
}

}  // namespace ui
