#pragma once

#include <SDL3/SDL.h>

#include <mutex>
#include <optional>
#include <string>

namespace platform {

// Thin wrapper over SDL_ShowOpenFolderDialog, used to pick an export destination
// directory (Phase 10). Mirrors FileDialog: the SDL callback may run on another
// thread, so the chosen path lands in a mutex-guarded slot and is delivered to
// the main thread by polling take_result() once per frame.
//
// LIFETIME ASSUMPTION: a FolderDialog is owned by App and lives for the entire
// application run (the async callback captures `this`).
class FolderDialog {
public:
    void open(SDL_Window* parent);   // pick a single existing folder

    [[nodiscard]] bool busy() const noexcept;

    // Non-nullopt exactly once after the dialog closes. Empty string => cancelled.
    [[nodiscard]] std::optional<std::string> take_result();

private:
    enum class St { Idle, Open, Done };
    static void SDLCALL on_folder(void* userdata, const char* const* filelist, int filter);

    mutable std::mutex mtx_;
    St                 state_ = St::Idle;
    std::string        path_;
};

} // namespace platform
