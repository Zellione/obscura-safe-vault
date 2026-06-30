#include "gfx/theme.h"

#include <array>
#include <utility>

namespace gfx {

namespace {

// The preset palettes. Every token of every preset is filled (validated by
// tests/gfx/test_theme.cpp). Order matches the ThemeId enum.
constexpr std::array<Theme, THEME_COUNT> PRESETS = {{
    // RefinedSlate — the original palette: deep slate base, one violet accent.
    Theme{
        .bg         = {15, 17, 21, 255},
        .surface    = {26, 29, 36, 255},
        .surface_hi = {35, 38, 46, 255},
        .border     = {38, 42, 51, 255},
        .accent     = {139, 124, 246, 255},
        .accent_dim = {90, 60, 150, 255},
        .text       = {223, 227, 234, 255},
        .text_dim   = {139, 147, 161, 255},
        .text_faint = {91, 98, 112, 255},
        .folder     = {200, 170, 90, 255},
        .favorite   = {245, 205, 70, 255},
        .danger     = {230, 120, 120, 255},
        .warn       = {230, 200, 110, 255},
        .ok         = {130, 220, 140, 255},
        .img_bg     = {12, 12, 16, 255},
        .strip_bg   = {20, 22, 27, 255},
    },
    // Light — bright surfaces, dark ink; the same violet accent darkened for
    // contrast on light backgrounds.
    Theme{
        .bg         = {245, 246, 248, 255},
        .surface    = {255, 255, 255, 255},
        .surface_hi = {232, 235, 240, 255},
        .border     = {210, 214, 222, 255},
        .accent     = {99, 84, 206, 255},
        .accent_dim = {180, 170, 235, 255},
        .text       = {28, 32, 40, 255},
        .text_dim   = {90, 98, 112, 255},
        .text_faint = {140, 148, 162, 255},
        .folder     = {176, 138, 40, 255},
        .favorite   = {214, 158, 20, 255},
        .danger     = {198, 60, 60, 255},
        .warn       = {176, 130, 20, 255},
        .ok         = {40, 150, 70, 255},
        .img_bg     = {225, 227, 232, 255},
        .strip_bg   = {236, 238, 242, 255},
    },
    // HighContrast — pure black base, bright white ink, vivid yellow accent and
    // strong borders for maximum legibility.
    Theme{
        .bg         = {0, 0, 0, 255},
        .surface    = {16, 16, 16, 255},
        .surface_hi = {40, 40, 40, 255},
        .border     = {120, 120, 120, 255},
        .accent     = {255, 234, 0, 255},
        .accent_dim = {120, 110, 0, 255},
        .text       = {255, 255, 255, 255},
        .text_dim   = {220, 220, 220, 255},
        .text_faint = {180, 180, 180, 255},
        .folder     = {255, 214, 0, 255},
        .favorite   = {255, 234, 0, 255},
        .danger     = {255, 90, 90, 255},
        .warn       = {255, 200, 0, 255},
        .ok         = {0, 255, 120, 255},
        .img_bg     = {0, 0, 0, 255},
        .strip_bg   = {12, 12, 12, 255},
    },
    // Midnight — deep navy base with an azure accent; a cooler dark theme.
    Theme{
        .bg         = {10, 14, 28, 255},
        .surface    = {18, 24, 44, 255},
        .surface_hi = {28, 36, 62, 255},
        .border     = {34, 44, 74, 255},
        .accent     = {86, 156, 255, 255},
        .accent_dim = {44, 86, 160, 255},
        .text       = {214, 224, 244, 255},
        .text_dim   = {130, 146, 180, 255},
        .text_faint = {86, 100, 134, 255},
        .folder     = {214, 180, 96, 255},
        .favorite   = {245, 205, 90, 255},
        .danger     = {236, 118, 128, 255},
        .warn       = {236, 196, 116, 255},
        .ok         = {120, 214, 160, 255},
        .img_bg     = {6, 9, 20, 255},
        .strip_bg   = {12, 17, 32, 255},
    },
}};

// Display labels and stable persistence slugs, indexed by ThemeId.
constexpr std::array<const char*, THEME_COUNT> NAMES = {
    "Refined Slate", "Light", "High Contrast", "Midnight",
};
constexpr std::array<const char*, THEME_COUNT> SLUGS = {
    "refined-slate", "light", "high-contrast", "midnight",
};

[[nodiscard]] bool in_range(ThemeId id) noexcept
{
    return std::to_underlying(id) < THEME_COUNT;
}

// Array index for a known-in-range id (underlying value → size_t; the cast is
// integral→integral, never enum→integral, so it stays clear of cpp:S7035).
[[nodiscard]] std::size_t index_of(ThemeId id) noexcept
{
    return static_cast<std::size_t>(std::to_underlying(id));
}

// The single active theme. Held as a function-local static so set_theme() can
// mutate it in place: the `theme::X` references bind to its members once and
// then track every swap.
Theme& mutable_active() noexcept
{
    static Theme active = PRESETS[0];
    return active;
}

ThemeId& active_id_slot() noexcept
{
    static ThemeId id = ThemeId::RefinedSlate;
    return id;
}

} // namespace

const Theme& theme_preset(ThemeId id) noexcept
{
    return PRESETS[in_range(id) ? index_of(id) : 0];
}

const char* theme_name(ThemeId id) noexcept
{
    return NAMES[in_range(id) ? index_of(id) : 0];
}

const char* theme_slug(ThemeId id) noexcept
{
    return SLUGS[in_range(id) ? index_of(id) : 0];
}

ThemeId theme_from_slug(std::string_view slug) noexcept
{
    for (int i = 0; i < THEME_COUNT; ++i)
        if (slug == SLUGS[static_cast<std::size_t>(i)])
            return static_cast<ThemeId>(i);
    return ThemeId::RefinedSlate;   // unknown / absent → default
}

const Theme& active_theme() noexcept { return mutable_active(); }
ThemeId      active_theme_id() noexcept { return active_id_slot(); }

void set_theme(ThemeId id) noexcept
{
    const ThemeId resolved = in_range(id) ? id : ThemeId::RefinedSlate;
    mutable_active()   = theme_preset(resolved);
    active_id_slot()   = resolved;
}

namespace theme {

// Bind each token to the active theme's member. mutable_active() is in this
// translation unit, so these are valid; the binding tracks in-place mutation.
const Color& BG         = mutable_active().bg;
const Color& SURFACE    = mutable_active().surface;
const Color& SURFACE_HI = mutable_active().surface_hi;
const Color& BORDER     = mutable_active().border;
const Color& ACCENT     = mutable_active().accent;
const Color& ACCENT_DIM = mutable_active().accent_dim;
const Color& TEXT       = mutable_active().text;
const Color& TEXT_DIM   = mutable_active().text_dim;
const Color& TEXT_FAINT = mutable_active().text_faint;
const Color& FOLDER     = mutable_active().folder;
const Color& FAVORITE   = mutable_active().favorite;
const Color& DANGER     = mutable_active().danger;
const Color& WARN       = mutable_active().warn;
const Color& OK         = mutable_active().ok;
const Color& IMG_BG     = mutable_active().img_bg;
const Color& STRIP_BG   = mutable_active().strip_bg;

} // namespace theme

} // namespace gfx
