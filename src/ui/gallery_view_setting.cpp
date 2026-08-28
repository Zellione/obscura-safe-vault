#include "ui/gallery_view_setting.h"

namespace ui {

namespace {
// The shared gallery view slot. Held as a function-local static (like the
// active-theme slot in gfx/theme.cpp and the autoplay/clipboard globals) so it
// stays mutable without a namespace-scope global variable.
GalleryView& view_slot() noexcept
{
    static GalleryView view = GalleryView::GridM;  // default until App seeds it
    return view;
}
}  // namespace

GalleryView gallery_view_setting() noexcept
{
    return view_slot();
}

void set_gallery_view_setting(GalleryView view) noexcept
{
    view_slot() = view;
}

}  // namespace ui