#include "app.h"

#include <print>

#include "gfx/renderer.h"

#ifndef OSV_DEFAULT_FONT
#define OSV_DEFAULT_FONT "assets/fonts/NotoSans-Regular.ttf"
#endif

namespace app {

bool App::init()
{
    if (!window_.init()) {
        std::println(stderr, "[App] Window initialisation failed.");
        return false;
    }

    // Phase 4: bake the UI font atlas. A missing font is non-fatal — the app
    // still runs, just without text.
    font_ready_ = font_.bake_from_file(OSV_DEFAULT_FONT, 28.0f);
    if (!font_ready_)
        std::println(stderr, "[App] Font atlas unavailable ('{}').", OSV_DEFAULT_FONT);

    std::println("[App] Initialised (Phase 4 — graphics layer).");
    return true;
}

void App::run()
{
    bool quit = false;
    while (!quit) {
        window_.process_events(quit);

        // Clear to the dark accent colour, then draw the Phase 4 demo: a coloured
        // rectangle and a text label rendered from the baked font atlas.
        window_.begin_frame(18, 18, 24);

        gfx::Renderer r(window_.sdl_renderer());
        r.draw_rect(SDL_FRect{40.0f, 40.0f, 220.0f, 120.0f},
                    gfx::Color{120, 80, 200, 255});
        if (font_ready_)
            r.draw_text(font_, 48.0f, 84.0f, "Obscura-Safe-Vault",
                        gfx::Color{240, 240, 245, 255});

        window_.end_frame();
    }
}

void App::shutdown()
{
    // Drop the font's GPU texture before the window destroys the renderer that
    // owns it.
    font_.release_texture();

    // Future phases will tear down subsystems here in reverse-init order:
    //   image cache, vault (zero master key in memory), gfx, SDL.
    window_.shutdown();
    std::println("[App] Clean shutdown.");
}

} // namespace app
