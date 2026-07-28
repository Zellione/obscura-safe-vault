#pragma once

// Phase 56: the single source of truth for how far apart two lines of text may
// be stacked. The app bakes its font at 28 px while several surfaces laid text
// out on a 20–25.5 px pitch, so adjacent lines' ink overlapped and a clip band
// sized to a whole number of those pitches cut the bottom line's descenders.
//
// The rule: a pitch is ALWAYS derived from the live font, never written as a
// literal, and is always strictly greater than the glyph box.

namespace ui {

// Vertical advance between two consecutive lines of `font_px` text. 1.25 is the
// standard reading-comfort leading and, more to the point, guarantees
// pitch > font_px so ink cannot touch. Whole pixels, so a band that is a whole
// number of pitches lands on integer boundaries and never half-clips a row.
// Returns 0 for a degenerate (unbaked) font rather than a negative advance.
[[nodiscard]] float line_pitch(float font_px) noexcept;

// Height of a row that holds one line of text with `pad` above and below.
// Negative padding is treated as zero — a row is never shorter than its text.
[[nodiscard]] float row_height(float font_px, float pad) noexcept;

} // namespace ui
