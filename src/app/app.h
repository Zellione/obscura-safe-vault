#pragma once

#include <memory>
#include <string>

#include "gfx/text.h"
#include "gfx/texture_cache.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "platform/folder_dialog.h"
#include "ui/screen.h"
#include "vault/vault.h"

namespace app {

enum class State { Locked, Browsing, Viewing };

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
    void to_gallery(const std::string& path = {}, int selected = 0);
    void to_viewer(const std::string& gallery_path, int index);
    void to_favorite_images();
    void to_favorite_galleries();

    // run() helpers (kept small so the loop stays readable).
    void dispatch_event(const SDL_Event& e);     // quit/close here, else to screen
    bool pump_events(bool animating);            // wait/poll + dispatch; had-event?
    bool apply_nav();                            // resolve a transition; transitioned?
    void render_frame();                         // draw + present + frame-cap fallback

    gfx::Window                        window_;
    gfx::FontAtlas                     font_;
    bool                               font_ready_ = false;
    std::unique_ptr<gfx::TextureCache> cache_;
    platform::FileDialog               dialog_;
    platform::FolderDialog             folder_dialog_;
    vault::Vault                       vault_;
    std::unique_ptr<ui::Screen>        screen_;
    State                              state_   = State::Locked;
    bool                               running_ = false;   // main-loop run flag
};

} // namespace app
