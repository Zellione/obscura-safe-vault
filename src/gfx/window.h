#pragma once

#include <SDL3/SDL.h>
#include <string>

namespace gfx {

struct WindowConfig {
    std::string title    = "Obscura-Safe-Vault";
    int         width    = 1280;
    int         height   = 800;
    bool        resizable = true;
};

/// Owns the SDL_Window + SDL_Renderer pair.
/// Lifetime: init() → { process_events() / begin_frame() / end_frame() } → shutdown().
class Window {
public:
    Window()  = default;
    ~Window() { shutdown(); }

    // Non-copyable, non-movable
    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&)                 = delete;
    Window& operator=(Window&&)      = delete;

    /// Initialise SDL, create window + renderer.
    /// Returns false on failure (error logged to stderr).
    [[nodiscard]] bool init(const WindowConfig& cfg = {});

    /// Destroy renderer, window, and SDL subsystems.
    void shutdown();

    /// Poll all pending events. Sets quit=true when the window is closed or
    /// Escape is pressed. Returns false only if SDL itself has a fatal error.
    bool process_events(bool& quit);

    /// Clear the renderer to the given RGBA colour (0 = opaque black).
    void begin_frame(uint8_t r = 18, uint8_t g = 18, uint8_t b = 24, uint8_t a = 255);

    /// Present the rendered frame.
    void end_frame();

    // Accessors
    SDL_Window*   sdl_window()   const noexcept { return window_; }
    SDL_Renderer* sdl_renderer() const noexcept { return renderer_; }
    int           width()        const noexcept { return width_; }
    int           height()       const noexcept { return height_; }

private:
    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    int           width_    = 0;
    int           height_   = 0;
};

} // namespace gfx
