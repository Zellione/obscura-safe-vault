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
    // What the currently-open dialog is collecting. Lets one shared FileDialog
    // be polled by several handlers without one stealing another's result —
    // take_result(Purpose) only resolves for the matching kind.
    enum class Purpose { None, Vault, Images, Keyfile, Zip, SaveKeyfile, SaveVault };

    void open_vault(SDL_Window* parent);    // *.osv (single)
    void open_images(SDL_Window* parent);    // common image types (multi)
    void open_keyfile(SDL_Window* parent);   // any file (single)
    void open_zip(SDL_Window* parent);       // *.zip (single)
    void save_keyfile(SDL_Window* parent);   // pick a location for a new keyfile
    void save_vault(SDL_Window* parent);     // pick a location for a new *.osv vault

    [[nodiscard]] bool busy() const noexcept;

    // Non-nullopt exactly once after a dialog closes. Empty vector => cancelled.
    [[nodiscard]] std::optional<std::vector<std::string>> take_result();

    // Purpose-scoped take: resolves only when the closed dialog was opened for
    // `want`; otherwise leaves the result untouched for another poller. Use this
    // when one FileDialog is drained by more than one handler in the same frame
    // (e.g. GalleryGrid's image-import vs zip-import pollers).
    [[nodiscard]] std::optional<std::vector<std::string>> take_result(Purpose want);

private:
    enum class St { Idle, Open, Done };
    static void SDLCALL on_files(void* userdata, const char* const* filelist, int filter);

    // Transition Idle -> Open for `purpose` and clear any prior result. Returns
    // false if a dialog is already open (caller should bail without showing one).
    bool begin_open(Purpose purpose);

    mutable std::mutex       mtx_;
    St                       state_ = St::Idle;
    Purpose                  purpose_ = Purpose::None;
    std::vector<std::string> paths_;

    friend struct FileDialogTestPeer;   // tests/platform/test_file_dialog.cpp
};

} // namespace platform
