#include "window.h"

#include <cstdio>

namespace gfx {

bool Window::init(const WindowConfig& cfg)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "[gfx::Window] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (cfg.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    window_ = SDL_CreateWindow(cfg.title.c_str(), cfg.width, cfg.height, flags);
    if (!window_) {
        std::fprintf(stderr, "[gfx::Window] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    // Request GPU-accelerated renderer; fall back to software if unavailable.
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        std::fprintf(stderr, "[gfx::Window] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_Quit();
        return false;
    }

    SDL_GetWindowSize(window_, &width_, &height_);
    return true;
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

bool Window::process_events(bool& quit)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            quit = true;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (e.key.key == SDLK_ESCAPE) {
                quit = true;
            }
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            width_  = e.window.data1;
            height_ = e.window.data2;
            break;
        default:
            break;
        }
    }
    return true;
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
