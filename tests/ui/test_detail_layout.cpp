// Phase 56: the detail panel's vertical rhythm as a pure function. The bug this
// guards is a row whose 28 px glyph box reached into the row below it, because
// the body pitch was a hardcoded 24. The property that forbids it — consecutive
// body lines never overlap — is asserted here rather than eyeballed.

#include "test_framework.h"

#include <span>
#include <vector>

#include "ui/detail_layout.h"
#include "ui/detail_model.h"

namespace {

constexpr float FONT_PX = 28.0f;

ui::DetailContent sample()
{
    ui::DetailContent c;
    c.heading    = "beach.jpg";
    c.subheading = "★ favorite";
    c.sections.push_back({.title = "",
                          .rows = {{"Type", "JPEG"}, {"Size", "2.1 MB"}},
                          .bullets = {},
                          .is_tags = false});
    c.sections.push_back({.title = "Tags", .rows = {}, .bullets = {"summer", "sea"},
                          .is_tags = true});
    return c;
}

} // namespace

TEST(detail_layout_body_lines_never_overlap)
{
    const auto lines = ui::layout_detail_lines(sample(), ui::detail_metrics(FONT_PX));
    REQUIRE(lines.size() > 1);
    for (size_t i = 1; i < lines.size(); ++i) {
        CHECK(lines[i - 1].y + FONT_PX <= lines[i].y);
    }
}

TEST(detail_layout_reproduces_the_drawing_order)
{
    const auto lines = ui::layout_detail_lines(sample(), ui::detail_metrics(FONT_PX));
    REQUIRE(lines.size() == 7);
    CHECK(lines[0].kind == ui::DetailLineKind::Heading);
    CHECK(lines[1].kind == ui::DetailLineKind::Subheading);
    CHECK(lines[2].kind == ui::DetailLineKind::Row);       // metadata section has no title
    CHECK(lines[3].kind == ui::DetailLineKind::Row);
    CHECK(lines[4].kind == ui::DetailLineKind::TagSectionTitle);
    CHECK(lines[5].kind == ui::DetailLineKind::TagBullet);
    CHECK(lines[6].kind == ui::DetailLineKind::TagBullet);
}

TEST(detail_layout_indexes_back_into_the_content_model)
{
    const auto lines = ui::layout_detail_lines(sample(), ui::detail_metrics(FONT_PX));
    REQUIRE(lines.size() == 7);
    CHECK_EQ(lines[2].section, static_cast<size_t>(0));
    CHECK_EQ(lines[2].item, static_cast<size_t>(0));
    CHECK_EQ(lines[3].item, static_cast<size_t>(1));
    CHECK_EQ(lines[5].section, static_cast<size_t>(1));
    CHECK_EQ(lines[6].item, static_cast<size_t>(1));
}

TEST(detail_layout_charges_a_section_gap_before_each_section)
{
    const ui::DetailMetrics m = ui::detail_metrics(FONT_PX);
    const auto lines = ui::layout_detail_lines(sample(), m);
    REQUIRE(lines.size() == 7);
    // Second section's title starts a gap below the last row of the first.
    CHECK_EQ(lines[4].y, lines[3].y + lines[3].height + m.section_gap);
}

TEST(detail_layout_gives_tag_bullets_a_full_chip_line)
{
    const ui::DetailMetrics m = ui::detail_metrics(FONT_PX);
    const auto lines = ui::layout_detail_lines(sample(), m);
    REQUIRE(lines.size() == 7);
    CHECK_EQ(lines[5].height, m.chip_line_h);
    CHECK_EQ(lines[2].height, m.pitch);      // an ordinary row uses the body pitch
}

TEST(detail_layout_body_pitch_clears_the_glyph_box)
{
    CHECK(ui::detail_metrics(FONT_PX).pitch > FONT_PX);
    CHECK(ui::detail_metrics(FONT_PX).chip_line_h > 0.0f);
}

TEST(detail_layout_omits_an_empty_subheading)
{
    ui::DetailContent c = sample();
    c.subheading.clear();
    const auto lines = ui::layout_detail_lines(c, ui::detail_metrics(FONT_PX));
    REQUIRE(!lines.empty());
    CHECK(lines[0].kind == ui::DetailLineKind::Heading);
    CHECK(lines[1].kind != ui::DetailLineKind::Subheading);
}

TEST(detail_layout_height_is_the_end_of_the_last_line)
{
    const auto lines = ui::layout_detail_lines(sample(), ui::detail_metrics(FONT_PX));
    REQUIRE(!lines.empty());
    const ui::DetailLine& last = lines.back();
    CHECK_EQ(ui::detail_content_height(lines), last.y + last.height);
}

TEST(detail_layout_always_reserves_the_heading_line)
{
    // The heading is unconditional: the old drawing loop reserved HEADING_H even
    // when the heading was empty, and callers clamp their scroll against the
    // returned content height. Preserve that exactly.
    const ui::DetailMetrics m = ui::detail_metrics(FONT_PX);
    const auto lines = ui::layout_detail_lines({}, m);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].kind == ui::DetailLineKind::Heading);
    CHECK_EQ(lines[0].y, 0.0f);
    CHECK_EQ(lines[0].height, m.heading_h);
    CHECK_EQ(ui::detail_content_height(lines), m.heading_h);
}
