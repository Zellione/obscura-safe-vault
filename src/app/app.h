#pragma once

#include <memory>

#include "gfx/text.h"
#include "gfx/texture_cache.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "ui/screen.h"
#include "vault/vault.h"

namespace app {

enum class State { Locked, Browsing }; // Viewing reserved for Phase 6

class App {
public:
    App()  = default;
    ~App() = default;

    App(const App&)            = delete;
    App& operator=(const App&) = delete;

    [[nodiscard]] bool init();
    void run();
    void shutdown();

private:
    void to_unlock();
    void to_gallery();

    gfx::Window                        window_;
    gfx::FontAtlas                     font_;
    bool                               font_ready_ = false;
    std::unique_ptr<gfx::TextureCache> cache_;
    platform::FileDialog               dialog_;
    vault::Vault                       vault_;
    std::unique_ptr<ui::Screen>        screen_;
    State                              state_ = State::Locked;
};

} // namespace app
