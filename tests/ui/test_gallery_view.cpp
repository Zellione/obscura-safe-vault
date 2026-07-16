#include "test_framework.h"

#include "ui/gallery_view.h"

using ui::GalleryView;

// --- cell_size_for (Phase 40 Part 3: 5-way List/Grid view density) ---------

TEST(cell_size_for_returns_four_distinct_grid_sizes)
{
    const float s  = ui::cell_size_for(GalleryView::GridS);
    const float m  = ui::cell_size_for(GalleryView::GridM);
    const float l  = ui::cell_size_for(GalleryView::GridL);
    const float xl = ui::cell_size_for(GalleryView::GridXL);

    CHECK(s < m);
    CHECK(m < l);
    CHECK(l < xl);
}

TEST(cell_size_for_grid_m_matches_legacy_fixed_cell_size)
{
    // GridM must equal the pre-Phase-40-Part-3 fixed CELL constant exactly, so
    // existing sessions render identically until a user opts into a different
    // density.
    CHECK_EQ(ui::cell_size_for(GalleryView::GridM), 188.0);
}

// --- next_gallery_view (the L-key cycle) ------------------------------------

TEST(next_gallery_view_cycles_list_to_grid_s_to_m_to_l_to_xl_and_wraps)
{
    using enum GalleryView;
    CHECK(ui::next_gallery_view(List)   == GridS);
    CHECK(ui::next_gallery_view(GridS)  == GridM);
    CHECK(ui::next_gallery_view(GridM)  == GridL);
    CHECK(ui::next_gallery_view(GridL)  == GridXL);
    CHECK(ui::next_gallery_view(GridXL) == List);
}
