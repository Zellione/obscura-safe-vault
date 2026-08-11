#include "ui/dual_layout.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <string>

namespace ui {

DualSplit dual_split(float win_w, float win_h) noexcept
{
    const float w   = std::max(0.0f, win_w);
    const float h   = std::max(0.0f, win_h);
    const float div = std::min(2.0f, w);                       // divider strip
    const float lw  = std::floor((w - div) / 2.0f);
    const float rw  = w - div - lw;
    DualSplit s;
    s.left    = {0.0f, 0.0f, lw, h};
    s.divider = {lw, 0.0f, div, h};
    s.right   = {lw + div, 0.0f, rw, h};
    return s;
}

int pane_at(const DualSplit& s, float x) noexcept
{
    return x < s.divider.x + s.divider.w / 2.0f ? 0 : 1;
}

DualTransferRefusal dual_transfer_check(std::string_view src_gallery,
                                        std::string_view dst_gallery,
                                        std::span<const std::string> selected_gallery_paths) noexcept
{
    if (src_gallery == dst_gallery) return DualTransferRefusal::SameGallery;
    for (const std::string& g : selected_gallery_paths) {
        if (dst_gallery == g) return DualTransferRefusal::IntoOwnSubtree;
        if (dst_gallery.size() > g.size() && dst_gallery.starts_with(g) &&
            dst_gallery[g.size()] == '/')
            return DualTransferRefusal::IntoOwnSubtree;
    }
    return DualTransferRefusal::None;
}

} // namespace ui
