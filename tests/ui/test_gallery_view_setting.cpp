#include "test_framework.h"

#include "ui/gallery_view_setting.h"

using ui::gallery_view_setting;
using ui::GalleryView;
using ui::set_gallery_view_setting;

TEST(gallery_view_setting_defaults_to_grid_m_before_seed)
{
    // Independent of any other test: the slot default is GridM, so a surface
    // that reads it before App::init seeds gets the middle density.
    CHECK(gallery_view_setting() == GalleryView::GridM);
}

TEST(gallery_view_setting_set_then_get_round_trips)
{
    set_gallery_view_setting(GalleryView::GridXXL);
    CHECK(gallery_view_setting() == GalleryView::GridXXL);
    set_gallery_view_setting(GalleryView::List);
    CHECK(gallery_view_setting() == GalleryView::List);
    // Restore the default so later tests are order-independent.
    set_gallery_view_setting(GalleryView::GridM);
}