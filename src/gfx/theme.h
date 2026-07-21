#pragma once

#include <cstdint>
#include <string_view>

#include "gfx/color.h"

// UI colour theming (Phase 23). Historically a single "Refined Slate" palette of
// compile-time constants; now a runtime-selectable value so the user can pick a
// theme that applies immediately and persists across launches.
//
// `Theme` is the full token set; the built-in presets fill every token. One
// active theme is held globally — `active_theme()` reads it and `set_theme()`
// swaps it. Existing `gfx::theme::X` call sites are unchanged: those tokens are
// now references into the active theme, so a switch is picked up everywhere.
namespace gfx {

// Every UI colour token. Presets fill all of them; geometry (corner radii) is
// theme-independent and lives in `gfx::theme` below.
struct Theme {
    Color bg;            // window background
    Color surface;       // tiles, fields, panels
    Color surface_hi;    // hover / selected fill
    Color border;        // hairline borders
    Color accent;        // selection ring, focus, primary
    Color accent_dim;    // pressed / active fill
    Color text;          // primary text
    Color text_dim;      // secondary text
    Color text_faint;    // hints / key legends
    Color folder;        // folder glyph
    Color favorite;      // favorite (bookmark) badge
    Color danger;        // error text
    Color warn;          // medium strength
    Color ok;            // strong strength
    Color img_bg;        // viewer image backdrop
    Color strip_bg;      // thumbnail strip backdrop
};

// Built-in presets. RefinedSlate is the default (the original palette).
enum class ThemeId : std::uint8_t {
    RefinedSlate = 0,
    Light,
    HighContrast,
    Midnight,
};
inline constexpr int THEME_COUNT = 4;

// Active selection. set_theme() takes effect immediately for every `theme::X`
// call site; an out-of-range id is clamped to the default.
[[nodiscard]] const Theme& active_theme() noexcept;
[[nodiscard]] ThemeId      active_theme_id() noexcept;
void                       set_theme(ThemeId id) noexcept;

// Preset table + presentation metadata (pure).
[[nodiscard]] const Theme& theme_preset(ThemeId id) noexcept;   // out-of-range → default
[[nodiscard]] const char*  theme_name(ThemeId id) noexcept;     // human label for the picker
[[nodiscard]] const char*  theme_slug(ThemeId id) noexcept;     // stable persistence token
[[nodiscard]] ThemeId      theme_from_slug(std::string_view slug) noexcept;  // unknown → default


// Tag-category colour palette (Phase 49). A category stores a swatch INDEX, not
// an RGB, so the same vault reads correctly under every theme. Each swatch is
// defined twice — one variant for a dark window background, one for a light one —
// because the chip paints the tag TEXT in this colour, and a pastel that reads
// well on slate is invisible on white. tag_swatch() picks the variant from the
// active theme's background luminance.
//
// TAG_SWATCH_COUNT must stay in lockstep with vault::TAG_SWATCH_COUNT, which
// bounds the persisted byte; ui/tag_chip.cpp static_asserts that they agree.
inline constexpr int TAG_SWATCH_COUNT = 16;

[[nodiscard]] Color       tag_swatch(int index) noexcept;       // out of range → TEXT_DIM
[[nodiscard]] const char* tag_swatch_name(int index) noexcept;  // out of range → "Default"

namespace theme {

// Standard corner radius for surfaces/buttons/tiles (px). Theme-independent.
inline constexpr float RADIUS       = 10.0f;
inline constexpr float RADIUS_SMALL = 6.0f;

// Colour tokens — references into the active theme so a runtime switch is seen
// by every existing `theme::X` use with no change at the call site (Phase 23).
extern const Color& BG;
extern const Color& SURFACE;
extern const Color& SURFACE_HI;
extern const Color& BORDER;
extern const Color& ACCENT;
extern const Color& ACCENT_DIM;
extern const Color& TEXT;
extern const Color& TEXT_DIM;
extern const Color& TEXT_FAINT;
extern const Color& FOLDER;
extern const Color& FAVORITE;
extern const Color& DANGER;
extern const Color& WARN;
extern const Color& OK;
extern const Color& IMG_BG;
extern const Color& STRIP_BG;

} // namespace theme

} // namespace gfx
