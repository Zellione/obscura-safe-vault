#include "ui/gallery_view.h"

namespace ui {

float cell_size_for(GalleryView view) noexcept
{
    using enum GalleryView;
    switch (view) {
        case GridS:  return 128.0f;
        case GridM:  return 188.0f;
        case GridL:  return 248.0f;
        case GridXL: return 320.0f;
        case List:   return 0.0f;   // unused for list layout
    }
    return 188.0f;
}

GalleryView next_gallery_view(GalleryView view) noexcept
{
    using enum GalleryView;
    switch (view) {
        case List:   return GridS;
        case GridS:  return GridM;
        case GridM:  return GridL;
        case GridL:  return GridXL;
        case GridXL: return List;
    }
    return List;
}

} // namespace ui
