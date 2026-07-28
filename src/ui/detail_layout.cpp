#include "ui/detail_layout.h"

#include "ui/detail_model.h"
#include "ui/tag_chip.h"      // CHIP_LINE_H
#include "ui/text_metrics.h"

namespace ui {

DetailMetrics detail_metrics(float font_px)
{
    DetailMetrics m;
    m.pitch       = line_pitch(font_px);
    m.chip_line_h = CHIP_LINE_H;
    return m;
}

std::vector<DetailLine> layout_detail_lines(const DetailContent& content, const DetailMetrics& m)
{
    std::vector<DetailLine> lines;
    float y = 0.0f;

    const auto push = [&](DetailLineKind kind, float h, std::size_t section, std::size_t item) {
        lines.push_back({.kind = kind, .y = y, .height = h, .section = section, .item = item});
        y += h;
    };

    // Heading is unconditional: the old drawing loop reserved 34 px whether or not
    // the heading was empty, and the return value (content height) feeds the callers'
    // scroll clamp. Subheading is conditional on content.
    push(DetailLineKind::Heading, m.heading_h, 0, 0);
    if (!content.subheading.empty()) push(DetailLineKind::Subheading, m.subheading_h, 0, 0);

    for (std::size_t s = 0; s < content.sections.size(); ++s) {
        const DetailSection& sec = content.sections[s];
        y += m.section_gap;
        if (!sec.title.empty()) {
            // A tag section's title reserves a full chip line so the first chip's
            // ink clears the title's descenders (Phase 49 rule, preserved).
            push(sec.is_tags ? DetailLineKind::TagSectionTitle : DetailLineKind::SectionTitle,
                 sec.is_tags ? m.chip_line_h : m.title_h, s, 0);
        }
        for (std::size_t i = 0; i < sec.rows.size(); ++i)
            push(DetailLineKind::Row, m.pitch, s, i);
        for (std::size_t i = 0; i < sec.bullets.size(); ++i)
            push(sec.is_tags ? DetailLineKind::TagBullet : DetailLineKind::Bullet,
                 sec.is_tags ? m.chip_line_h : m.pitch, s, i);
    }
    return lines;
}

float detail_content_height(std::span<const DetailLine> lines)
{
    if (lines.empty()) return 0.0f;
    return lines.back().y + lines.back().height;
}

} // namespace ui
