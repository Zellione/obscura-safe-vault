#include "ui/gallery_view.h"

#include <array>
#include <string_view>

namespace ui {

float cell_size_for(GalleryView view) noexcept
{
    using enum GalleryView;
    switch (view) {
    case GridS:
        return 224.0f;
    case GridM:
        return 288.0f;
    case GridL:
        return 384.0f;
    case GridXL:
        return 480.0f;
    case GridXXL:
        return 512.0f;
    case List:
        return 0.0f;  // unused for list layout
    }
    return 288.0f;
}

GalleryView next_gallery_view(GalleryView view) noexcept
{
    using enum GalleryView;
    switch (view) {
    case List:
        return GridS;
    case GridS:
        return GridM;
    case GridM:
        return GridL;
    case GridL:
        return GridXL;
    case GridXL:
        return GridXXL;
    case GridXXL:
        return List;
    }
    return List;
}

GalleryView next_grid_density(GalleryView view) noexcept
{
    using enum GalleryView;
    switch (view) {
    case List:
        return GridS;
    case GridS:
        return GridM;
    case GridM:
        return GridL;
    case GridL:
        return GridXL;
    case GridXL:
        return GridXXL;
    case GridXXL:
        return GridS;
    }
    return GridS;
}

float grid_cell_size(GalleryView view) noexcept
{
    return cell_size_for(view == GalleryView::List ? GalleryView::GridM : view);
}

// Lookup table for labels, slugs, and view enumeration.
constexpr std::array GALLERY_VIEW_TABLE{
    std::pair{GalleryView::List, std::pair{std::string_view("List"), std::string_view("list")}},
    std::pair{GalleryView::GridS,
              std::pair{std::string_view("Grid S"), std::string_view("grid-s")}},
    std::pair{GalleryView::GridM,
              std::pair{std::string_view("Grid M"), std::string_view("grid-m")}},
    std::pair{GalleryView::GridL,
              std::pair{std::string_view("Grid L"), std::string_view("grid-l")}},
    std::pair{GalleryView::GridXL,
              std::pair{std::string_view("Grid XL"), std::string_view("grid-xl")}},
    std::pair{GalleryView::GridXXL,
              std::pair{std::string_view("Grid XXL"), std::string_view("grid-xxl")}},
};

std::string_view gallery_view_label(GalleryView view) noexcept
{
    for (const auto& [v, labels] : GALLERY_VIEW_TABLE) {
        if (v == view) {
            return labels.first;
        }
    }
    return "List";
}

GalleryView prev_gallery_view(GalleryView view) noexcept
{
    for (size_t i = 0; i < GALLERY_VIEW_TABLE.size(); ++i) {
        if (GALLERY_VIEW_TABLE[i].first == view) {
            // Found the view at index i, return the previous row's view (wrap around).
            size_t prev_index = (i == 0) ? (GALLERY_VIEW_TABLE.size() - 1) : (i - 1);
            return GALLERY_VIEW_TABLE[prev_index].first;
        }
    }
    return GalleryView::List;
}

std::string_view gallery_view_slug(GalleryView view) noexcept
{
    for (const auto& [v, labels] : GALLERY_VIEW_TABLE) {
        if (v == view) {
            return labels.second;
        }
    }
    return "grid-m";
}

GalleryView gallery_view_from_slug(std::string_view slug) noexcept
{
    for (const auto& [v, labels] : GALLERY_VIEW_TABLE) {
        if (labels.second == slug) {
            return v;
        }
    }
    return GalleryView::GridM;
}

} // namespace ui
