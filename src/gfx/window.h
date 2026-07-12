#pragma once

#include <SDL3/SDL.h>
#include <string>

namespace gfx {

// Pure: whether a window with these SDL_WindowFlags is actually visible on
// screen (not minimized, not hidden, not fully occluded by another window).
// Used to skip idle-heartbeat redraws when nothing would be seen anyway.
// Header-only (no SDL display needed) so it's linkable from tests, unlike the
// rest of Window, which needs a real windowing system to construct.
[[nodiscard]] inline bool window_flags_visible(SDL_WindowFlags flags) noexcept
{
    return (flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED)) == 0;
}

struct WindowConfig {
    std::string title    = "Obscura-Safe-Vault";
    int         width    = 1280;
    int         height   = 800;
    bool        resizable = true;
};

/// Owns the SDL_Window + SDL_Renderer pair.
/// Lifetime: init() → { poll_event() / begin_frame() / end_frame() } → shutdown().
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

    /// Poll one pending event into `out`. Returns false when the queue is empty.
    /// (App decides what quit/close means — the window no longer self-quits.)
    [[nodiscard]] bool poll_event(SDL_Event& out) const;

    /// Clear the renderer to the given RGBA colour (0 = opaque black).
    void begin_frame(uint8_t r = 18, uint8_t g = 18, uint8_t b = 24, uint8_t a = 255);

    /// Present the rendered frame.
    void end_frame();

    // Accessors
    SDL_Window*   sdl_window()   const noexcept { return window_; }
    SDL_Renderer* sdl_renderer() const noexcept { return renderer_; }

    // Live renderer output size in pixels — tracks window resizes and matches the
    // coordinate space the renderer actually draws into (incl. HiDPI scaling), so
    // layout reflows correctly. Queried each call (cheap); never cached/stale.
    int           width()        const noexcept;
    int           height()       const noexcept;

    // True when the renderer presents in sync with the display refresh. When
    // false (some software/headless backends), the app loop must cap its own
    // frame rate to avoid spinning the GPU at 100%.
    bool          vsync()        const noexcept { return vsync_; }

    // True unless the window is minimized/hidden/fully occluded — used to skip
    // the idle-heartbeat redraw when nothing would be visible anyway.
    bool is_visible() const noexcept
    {
        return window_ != nullptr && window_flags_visible(SDL_GetWindowFlags(window_));
    }

private:
    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool          vsync_    = false;
};

} // namespace gfx
