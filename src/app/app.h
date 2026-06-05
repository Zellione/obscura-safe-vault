#pragma once

#include "gfx/window.h"

namespace app {

/// Application-level state machine.
///
/// States (Phase 0 has only Running):
///   Locked   — vault locked; unlock screen shown     (Phase 5)
///   Browsing — gallery grid / breadcrumb navigation  (Phase 5–6)
///   Viewing  — full image viewer + thumbnail strip   (Phase 6)
///   Running  — Phase 0 placeholder: just clears the window
enum class State {
    Running,   // Phase 0 placeholder
    // TODO (Phase 5): Locked, Browsing
    // TODO (Phase 6): Viewing
};

class App {
public:
    App()  = default;
    ~App() = default;

    App(const App&)            = delete;
    App& operator=(const App&) = delete;

    /// Initialise SDL, create window, and prepare all subsystems.
    [[nodiscard]] bool init();

    /// Main event/render loop — runs until the user closes the window.
    void run();

    /// Tear down all subsystems in reverse-init order.
    void shutdown();

private:
    gfx::Window window_;
    State       state_ = State::Running;
};

} // namespace app
