#pragma once

#include <SDL3/SDL.h>

#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace platform {

// Thin wrapper over SDL_ShowOpenFileDialog. The SDL callback may run on another
// thread, so results land in a mutex-guarded slot and are delivered to the main
// thread by polling take_result() once per frame.
//
// LIFETIME ASSUMPTION: a FileDialog is owned by App and lives for the entire
// application run. The async callback captures `this`, so the object must never
// be destroyed while a dialog may still fire (e.g. don't stack-allocate one or
// drop it on vault lock).
class FileDialog {
public:
    void open_vault(SDL_Window* parent);    // *.osv (single)
    void open_images(SDL_Window* parent);    // common image types (multi)
    void open_keyfile(SDL_Window* parent);   // any file (single)
    void save_keyfile(SDL_Window* parent);   // pick a location for a new keyfile

    [[nodiscard]] bool busy() const noexcept;

    // Non-nullopt exactly once after a dialog closes. Empty vector => cancelled.
    [[nodiscard]] std::optional<std::vector<std::string>> take_result();

private:
    enum class St { Idle, Open, Done };
    static void SDLCALL on_files(void* userdata, const char* const* filelist, int filter);

    // Transition Idle -> Open and clear any prior result. Returns false if a
    // dialog is already open (caller should bail out without showing another).
    bool begin_open();

    mutable std::mutex       mtx_;
    St                       state_ = St::Idle;
    std::vector<std::string> paths_;
};

} // namespace platform
