#include "ui/anim_repair.h"

#include "image/anim_info.h"
#include "vault/index.h"
#include "vault/vault.h"

namespace ui {

bool maybe_repair_animated(vault::Vault& v, std::string_view gallery_path,
                           const vault::IndexNode& node,
                           std::span<const uint8_t> data)
{
    if (node.type != vault::IndexNode::Type::Image) {
        return false;
    }
    if (!vault::format_can_animate(node.meta.format)) {
        return false;
    }

    const bool actual =
        image::is_animated(static_cast<image::ImageFormat>(node.meta.format), data);
    if (actual == node.meta.animated) {
        return false;
    }

    // Build the node path from gallery_path + node.name
    const auto base = std::string(gallery_path);
    const std::string node_path =
        base.empty() ? node.name : base + "/" + node.name;

    return v.repair_image_animated(node_path, actual);
}

bool AnimSniffGate::should_sniff(const vault::IndexNode& node)
{
    if (!node.is_image() || !vault::format_can_animate(node.meta.format) ||
        node.meta.animated) {
        return false;
    }
    return sniffed_.insert(node.meta.data_offset).second;
}

}  // namespace ui
