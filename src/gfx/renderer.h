#pragma once

// Phase 4 stub: GPU texture cache, image upload, text atlas.
// Placeholder until Phase 4 implementation lands.

#include <SDL3/SDL.h>

namespace gfx {

/// Thin wrapper around SDL_Renderer for higher-level draw operations.
/// Currently a stub; expanded in Phase 4.
class Renderer {
public:
    explicit Renderer(SDL_Renderer* r) : r_(r) {}

    SDL_Renderer* sdl() const noexcept { return r_; }

    // TODO (Phase 4): texture cache, draw_image, draw_text, draw_rect, …

private:
    SDL_Renderer* r_ = nullptr;
};

} // namespace gfx
