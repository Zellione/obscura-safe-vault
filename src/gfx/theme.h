#pragma once

#include "gfx/color.h"

// "Refined Slate" — the single source of truth for UI colours. Every screen and
// widget pulls its colours from here so the look stays consistent and a palette
// tweak is a one-file change. Deep slate base, hairline borders, one violet
// accent used sparingly for selection/focus.
namespace gfx::theme {

inline constexpr Color BG         {15, 17, 21, 255};    // window background
inline constexpr Color SURFACE    {26, 29, 36, 255};    // tiles, fields, panels
inline constexpr Color SURFACE_HI {35, 38, 46, 255};    // hover / selected fill
inline constexpr Color BORDER     {38, 42, 51, 255};    // hairline borders

inline constexpr Color ACCENT     {139, 124, 246, 255}; // selection ring, focus, primary
inline constexpr Color ACCENT_DIM {90, 60, 150, 255};   // pressed / active fill

inline constexpr Color TEXT       {223, 227, 234, 255}; // primary text
inline constexpr Color TEXT_DIM   {139, 147, 161, 255}; // secondary text
inline constexpr Color TEXT_FAINT {91, 98, 112, 255};   // hints / key legends

inline constexpr Color FOLDER     {200, 170, 90, 255};  // folder glyph
inline constexpr Color DANGER     {230, 120, 120, 255}; // error text
inline constexpr Color WARN       {230, 200, 110, 255}; // medium strength
inline constexpr Color OK         {130, 220, 140, 255}; // strong strength

inline constexpr Color IMG_BG     {12, 12, 16, 255};    // viewer image backdrop
inline constexpr Color STRIP_BG   {20, 22, 27, 255};    // thumbnail strip backdrop

// Standard corner radius for surfaces/buttons/tiles (px).
inline constexpr float RADIUS       = 10.0f;
inline constexpr float RADIUS_SMALL = 6.0f;

} // namespace gfx::theme
