#include "app.h"

#include <cstdio>

namespace app {

bool App::init()
{
    if (!window_.init()) {
        std::fprintf(stderr, "[App] Window initialisation failed.\n");
        return false;
    }
    std::printf("[App] Initialised (Phase 0 — window only).\n");
    return true;
}

void App::run()
{
    bool quit = false;
    while (!quit) {
        window_.process_events(quit);
        // Phase 0: clear to the application accent colour (dark near-black with a
        // slight purple tint) and present.  Future phases replace this with
        // real screen rendering dispatched via state_.
        window_.begin_frame(18, 18, 24);
        window_.end_frame();
    }
}

void App::shutdown()
{
    // Future phases will tear down subsystems here in reverse-init order:
    //   image cache, vault (zero master key in memory), gfx, SDL.
    window_.shutdown();
    std::printf("[App] Clean shutdown.\n");
}

} // namespace app
