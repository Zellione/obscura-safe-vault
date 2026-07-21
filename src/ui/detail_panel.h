#pragma once

#include <SDL3/SDL.h>

#include "ui/detail_model.h"

namespace gfx { class FontAtlas; class Renderer; }

// SDL drawing + open/scroll state for the gallery detail panel (Phase 48).
// The content itself is built by the pure `detail_model.*`.
namespace ui {

// Reserved width of the open panel, in pixels.
inline constexpr float DETAIL_PANEL_WIDTH = 280.0f;

// Below this window width the panel stays hidden even when toggled open — on a
// small window the grid needs the whole width more than the panel does.
inline constexpr float DETAIL_PANEL_MIN_WINDOW = 640.0f;

// Pixels one Ctrl+Up / Ctrl+Down step scrolls the panel.
inline constexpr float DETAIL_PANEL_SCROLL_STEP = 48.0f;

struct DetailPanelState {
    bool  open   = false;
    float scroll = 0.0f;
};

// Width the panel occupies: 0 when closed, and 0 when the window is too narrow.
// Pure so the reflow rule is unit-tested rather than re-derived per screen.
[[nodiscard]] float detail_panel_width(bool open, float window_width) noexcept;

// Draw `content` into `rect`, scrolled down by `scroll`. Rows outside `rect` are
// culled; every vault-derived string is middle-elided to the panel width.
// Returns the total content height so the caller can clamp its scroll.
float draw_detail_panel(gfx::Renderer& r, gfx::FontAtlas& font, const SDL_FRect& rect,
                        const DetailContent& content, float scroll);

// Ctrl+Up / Ctrl+Down scroll the open panel. Returns true when the key was
// consumed, so a host can fall through to grid navigation otherwise. The upper
// scroll bound is applied by the caller (draw_detail_panel returns the height).
bool handle_detail_panel_scroll(const SDL_KeyboardEvent& key, DetailPanelState& st);

} // namespace ui
