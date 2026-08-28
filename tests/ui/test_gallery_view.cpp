#include "test_framework.h"

#include "ui/gallery_view.h"

using ui::GalleryView;

// --- cell_size_for (Phase 40 Part 3: List/Grid view density) -----------------

TEST(cell_size_for_returns_five_distinct_grid_sizes)
{
    const float s = ui::cell_size_for(GalleryView::GridS);
    const float m = ui::cell_size_for(GalleryView::GridM);
    const float l = ui::cell_size_for(GalleryView::GridL);
    const float xl = ui::cell_size_for(GalleryView::GridXL);
    const float xxl = ui::cell_size_for(GalleryView::GridXXL);

    CHECK(s < m);
    CHECK(m < l);
    CHECK(l < xl);
    CHECK(xl < xxl);
}

TEST(cell_size_for_grid_m_matches_legacy_fixed_cell_size)
{
    // GridM was bumped in Phase 75 to 256, then Phase 93 to 288; this test
    // verifies the current value stays the largest stable middle default.
    CHECK_EQ(ui::cell_size_for(GalleryView::GridM), 288.0);
}

TEST(gallery_view_cell_sizes_phase93)
{
    using ui::GalleryView;
    CHECK(ui::cell_size_for(GalleryView::GridS) == 224.0f);
    CHECK(ui::cell_size_for(GalleryView::GridM) == 288.0f);
    CHECK(ui::cell_size_for(GalleryView::GridL) == 384.0f);
    CHECK(ui::cell_size_for(GalleryView::GridXL) == 480.0f);
    CHECK(ui::cell_size_for(GalleryView::GridXXL) == 512.0f);
    CHECK(ui::cell_size_for(GalleryView::List) == 0.0f);
}

// --- next_gallery_view (the L-key cycle) ------------------------------------

TEST(next_gallery_view_cycles_list_through_all_densities_and_wraps)
{
    using enum GalleryView;
    CHECK(ui::next_gallery_view(List) == GridS);
    CHECK(ui::next_gallery_view(GridS) == GridM);
    CHECK(ui::next_gallery_view(GridM) == GridL);
    CHECK(ui::next_gallery_view(GridL) == GridXL);
    CHECK(ui::next_gallery_view(GridXL) == GridXXL);
    CHECK(ui::next_gallery_view(GridXXL) == List);
}

// --- next_grid_density (grid-only surfaces: favorites / tags) ----------------

TEST(next_grid_density_cycles_grid_densities_only_skipping_list)
{
    using enum GalleryView;
    CHECK(ui::next_grid_density(List) == GridS);  // List is not in this cycle
    CHECK(ui::next_grid_density(GridS) == GridM);
    CHECK(ui::next_grid_density(GridM) == GridL);
    CHECK(ui::next_grid_density(GridL) == GridXL);
    CHECK(ui::next_grid_density(GridXL) == GridXXL);
    CHECK(ui::next_grid_density(GridXXL) == GridS);  // wraps within the densities
}

TEST(grid_cell_size_falls_back_to_grid_m_for_list)
{
    using enum GalleryView;
    // Grid-only surfaces render GridM when the shared setting is List.
    CHECK_EQ(ui::grid_view_for(List), GridM);
    CHECK_EQ(ui::grid_view_for(GridS), GridS);
    CHECK_EQ(ui::grid_view_for(GridXXL), GridXXL);
    CHECK_EQ(ui::grid_cell_size(List), ui::cell_size_for(GridM));
    CHECK_EQ(ui::grid_cell_size(GridS), ui::cell_size_for(GridS));
    CHECK_EQ(ui::grid_cell_size(GridXXL), ui::cell_size_for(GridXXL));
    CHECK(ui::grid_cell_size(GridS) < ui::grid_cell_size(GridXXL));
}

// --- gallery_view labels and slugs -------------------------------------------

TEST(gallery_view_labels_and_slugs)
{
    using ui::GalleryView;
    CHECK_EQ(ui::gallery_view_label(GalleryView::List), "List");
    CHECK_EQ(ui::gallery_view_label(GalleryView::GridXXL), "Grid XXL");
    CHECK_EQ(ui::gallery_view_slug(GalleryView::GridS), "grid-s");
    CHECK_EQ(ui::gallery_view_slug(GalleryView::GridXXL), "grid-xxl");
    CHECK_EQ(ui::gallery_view_from_slug("grid-l"), GalleryView::GridL);
    CHECK_EQ(ui::gallery_view_from_slug("nonsense"), GalleryView::GridM);
    CHECK_EQ(ui::gallery_view_from_slug(""), GalleryView::GridM);
    // Round-trip every value.
    for (auto v : {GalleryView::List, GalleryView::GridS, GalleryView::GridM, GalleryView::GridL,
                   GalleryView::GridXL, GalleryView::GridXXL}) {
        CHECK_EQ(ui::gallery_view_from_slug(ui::gallery_view_slug(v)), v);
    }
}

TEST(prev_gallery_view_inverts_next)
{
    using ui::GalleryView;
    for (auto v : {GalleryView::List, GalleryView::GridS, GalleryView::GridM, GalleryView::GridL,
                   GalleryView::GridXL, GalleryView::GridXXL}) {
        CHECK_EQ(ui::prev_gallery_view(ui::next_gallery_view(v)), v);
    }
}
