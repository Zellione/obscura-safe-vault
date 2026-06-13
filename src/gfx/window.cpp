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
