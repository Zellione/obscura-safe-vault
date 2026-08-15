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
    // GridM was bumped in Phase 75 to 256; this test now verifies the new size.
    CHECK_EQ(ui::cell_size_for(GalleryView::GridM), 256.0);
}

TEST(gallery_view_cell_sizes_phase75)
{
    using ui::GalleryView;
    CHECK(ui::cell_size_for(GalleryView::GridS)  == 192.0f);
    CHECK(ui::cell_size_for(GalleryView::GridM)  == 256.0f);
    CHECK(ui::cell_size_for(GalleryView::GridL)  == 352.0f);
    CHECK(ui::cell_size_for(GalleryView::GridXL) == 448.0f);
    CHECK(ui::cell_size_for(GalleryView::List)   == 0.0f);
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

// --- gallery_view labels and slugs -------------------------------------------

TEST(gallery_view_labels_and_slugs)
{
    using ui::GalleryView;
    CHECK_EQ(ui::gallery_view_label(GalleryView::List), "List");
    CHECK_EQ(ui::gallery_view_label(GalleryView::GridXL), "Grid XL");
    CHECK_EQ(ui::gallery_view_slug(GalleryView::GridS), "grid-s");
    CHECK_EQ(ui::gallery_view_from_slug("grid-l"), GalleryView::GridL);
    CHECK_EQ(ui::gallery_view_from_slug("nonsense"), GalleryView::GridM);
    CHECK_EQ(ui::gallery_view_from_slug(""), GalleryView::GridM);
    // Round-trip every value.
    for (auto v : {GalleryView::List, GalleryView::GridS, GalleryView::GridM,
                   GalleryView::GridL, GalleryView::GridXL}) {
        CHECK_EQ(ui::gallery_view_from_slug(ui::gallery_view_slug(v)), v);
    }
}

TEST(prev_gallery_view_inverts_next)
{
    using ui::GalleryView;
    for (auto v : {GalleryView::List, GalleryView::GridS, GalleryView::GridM,
                   GalleryView::GridL, GalleryView::GridXL}) {
        CHECK_EQ(ui::prev_gallery_view(ui::next_gallery_view(v)), v);
    }
}
