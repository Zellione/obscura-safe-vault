#pragma once

#include <SDL3/SDL.h>

#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace platform {

// Thin wrapper over SDL_ShowOpenFolderDialog, used to pick export destinations
// and import folders (Phase 10, Phase 51). Mirrors FileDialog: the SDL callback
// may run on another thread, so chosen paths land in a mutex-guarded slot and
// are delivered to the main thread by polling take_result() once per frame.
//
// LIFETIME ASSUMPTION: a FolderDialog is owned by App and lives for the entire
// application run (the async callback captures `this`).
class FolderDialog {
public:
    // What the currently-open dialog is collecting. Lets one shared FolderDialog
    // be polled by several handlers without one stealing another's result.
    enum class Purpose { None, Export, ImportFolder };

    void open(SDL_Window* parent, Purpose purpose, bool allow_many);

    [[nodiscard]] bool busy() const noexcept;

    // Non-nullopt exactly once after the dialog closes. Empty vector => cancelled.
    [[nodiscard]] std::optional<std::vector<std::string>> take_result();

    // Purpose-scoped take: resolves only when the closed dialog was opened for
    // `want`; otherwise leaves the result untouched for another poller.
    [[nodiscard]] std::optional<std::vector<std::string>> take_result(Purpose want);

private:
    enum class St { Idle, Open, Done };
    static void SDLCALL on_folder(void* userdata, const char* const* filelist, int filter);

    // Transition Idle -> Open for `purpose` and clear any prior result. Returns
    // false if a dialog is already open (caller should bail without showing one).
    bool begin_open(Purpose purpose);

    mutable std::mutex       mtx_;
    St                       state_ = St::Idle;
    Purpose                  purpose_ = Purpose::None;
    std::vector<std::string> paths_;

    friend struct FolderDialogTestPeer;   // tests/platform/test_folder_dialog.cpp
};

} // namespace platform
