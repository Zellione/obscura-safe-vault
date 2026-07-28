#pragma once

// Phase 56: the detail panel's vertical rhythm, extracted from its drawing loop.
// `detail_model.*` says WHAT the panel shows; this says WHERE each line goes and
// how tall it is; `detail_panel.cpp` only draws. Pure — no SDL, no FontAtlas —
// so the property that matters (consecutive lines never overlap under the live
// font) is a unit test rather than a visual inspection.

#include <cstddef>
#include <span>
#include <vector>

namespace ui {

struct DetailContent;   // ui/detail_model.h

enum class DetailLineKind {
    Heading,
    Subheading,
    SectionTitle,
    TagSectionTitle,   // a tag section's title reserves a full chip line
    Row,
    Bullet,
    TagBullet,         // drawn as colour-coded chips, not text
};

// One laid-out line. `y` is relative to the panel's content top (the caller adds
// its own padding and scroll). `section`/`item` index back into the DetailContent
// this was laid out from, so the drawer never re-walks the model in parallel.
struct DetailLine {
    DetailLineKind kind    = DetailLineKind::Row;
    float          y       = 0.0f;
    float          height  = 0.0f;
    std::size_t    section = 0;
    std::size_t    item    = 0;
};

// The fixed vertical measurements of the panel. Only `pitch` is font-derived:
// the others are deliberate block sizes that were already correct.
struct DetailMetrics {
    float pitch        = 0.0f;    // body row/bullet advance — ui::line_pitch(font_px)
    float heading_h    = 34.0f;
    float subheading_h = 24.0f;
    float title_h      = 22.0f;
    float section_gap  = 18.0f;
    float chip_line_h  = 30.0f;   // ui::CHIP_LINE_H
};

[[nodiscard]] DetailMetrics detail_metrics(float font_px);

// Lay `content` out in drawing order: heading, optional subheading, then each
// section preceded by a gap. A section with an empty title contributes no title
// line. Never returns a line for content that is not drawn.
[[nodiscard]] std::vector<DetailLine> layout_detail_lines(const DetailContent&  content,
                                                          const DetailMetrics& m);

// Total height the laid-out lines occupy — the value draw_detail_panel returns
// so its caller can clamp scrolling. Zero for an empty layout.
[[nodiscard]] float detail_content_height(std::span<const DetailLine> lines);

} // namespace ui
