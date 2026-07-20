#include "window.h"

#include <print>

namespace gfx {

bool Window::init(const WindowConfig& cfg)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::println(stderr, "[gfx::Window] SDL_Init failed: {}", SDL_GetError());
        return false;
    }

    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (cfg.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    window_ = SDL_CreateWindow(cfg.title.c_str(), cfg.width, cfg.height, flags);
    if (!window_) {
        std::println(stderr, "[gfx::Window] SDL_CreateWindow failed: {}", SDL_GetError());
        SDL_Quit();
        return false;
    }

    // Request GPU-accelerated renderer; fall back to software if unavailable.
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        std::println(stderr, "[gfx::Window] SDL_CreateRenderer failed: {}", SDL_GetError());
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_Quit();
        return false;
    }

    // Cap presentation to the display refresh so the render loop can't saturate
    // the GPU. Not every backend supports it (software/headless); when it fails
    // the app loop falls back to a manual frame-time floor (see App::run).
    vsync_ = SDL_SetRenderVSync(renderer_, 1);
    if (!vsync_)
        std::println(stderr, "[gfx::Window] VSync unavailable ({}); using frame cap.",
                     SDL_GetError());

    return true;
}

int Window::width() const noexcept
{
    int w = 0;
    int h = 0;
    if (renderer_) SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
    return w;
}

int Window::height() const noexcept
{
    int w = 0;
    int h = 0;
    if (renderer_) SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
    return h;
}

float Window::mouse_x() const noexcept
{
    float x = 0.0f;
    float y = 0.0f;
    if (window_ != nullptr) {
        SDL_GetMouseState(&x, &y);
    }
    return x;
}

float Window::mouse_y() const noexcept
{
    float x = 0.0f;
    float y = 0.0f;
    if (window_ != nullptr) {
        SDL_GetMouseState(&x, &y);
    }
    return y;
}

void Window::shutdown()
{
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

void Window::set_fullscreen(bool on)
{
    if (!window_ || on == fullscreen_) return;

    if (on) {
        SDL_GetWindowPosition(window_, &windowed_x_, &windowed_y_);
        SDL_GetWindowSize(window_, &windowed_w_, &windowed_h_);

        const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
        SDL_Rect bounds{};
        if (!SDL_GetDisplayUsableBounds(display, &bounds)) {
            std::println(stderr, "[gfx::Window] SDL_GetDisplayUsableBounds failed: {}",
                         SDL_GetError());
            return;   // stay windowed rather than resize to a garbage rect
        }

        SDL_SetWindowBordered(window_, false);
        SDL_SetWindowPosition(window_, bounds.x, bounds.y);
        SDL_SetWindowSize(window_, bounds.w, bounds.h);
    } else {
        SDL_SetWindowBordered(window_, true);
        SDL_SetWindowSize(window_, windowed_w_, windowed_h_);
        SDL_SetWindowPosition(window_, windowed_x_, windowed_y_);
    }
    fullscreen_ = on;
}

bool Window::poll_event(SDL_Event& out) const
{
    return SDL_PollEvent(&out);
}

void Window::begin_frame(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    SDL_SetRenderDrawColor(renderer_, r, g, b, a);
    SDL_RenderClear(renderer_);
}

void Window::end_frame()
{
    SDL_RenderPresent(renderer_);
}

} // namespace gfx
