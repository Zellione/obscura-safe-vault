#include "ui/clipboard.h"

#include <cstring>
#include <string>

#include <SDL3/SDL.h>
#include <monocypher.h>

#include "ui/text_input_model.h"

namespace ui {

namespace {

class SdlClipboard final : public ClipboardBackend {
public:
    char* get_text() override { return SDL_GetClipboardText(); }

    void release_text(char* p) override
    {
        if (p == nullptr) return;
        // SDL hands back a heap copy of the clipboard contents. If it held a
        // password, that copy is a second in-process exposure — wipe it before
        // freeing rather than leaving it in the allocator's free list.
        crypto_wipe(p, std::strlen(p));
        SDL_free(p);
    }

    bool set_text(std::string_view s) override
    {
        const std::string z(s);   // SDL needs NUL termination
        return SDL_SetClipboardText(z.c_str());
    }
};

ClipboardBackend* g_override = nullptr;

} // namespace

ClipboardBackend& clipboard_backend() noexcept
{
    static SdlClipboard sdl;
    return g_override != nullptr ? *g_override : static_cast<ClipboardBackend&>(sdl);
}

void set_clipboard_backend(ClipboardBackend* backend) noexcept { g_override = backend; }

bool paste_from_clipboard(ITextInput& field)
{
    ClipboardBackend& cb = clipboard_backend();
    char* raw = cb.get_text();
    if (raw == nullptr) return false;

    const size_t n = std::strlen(raw);
    if (n == 0) { cb.release_text(raw); return false; }

    // A string_view straight over the backend's buffer: for a secure field the
    // bytes go from here into mlock'd storage with no std::string in between.
    field.insert(std::string_view(raw, n));
    cb.release_text(raw);
    return true;
}

bool copy_selection_to_clipboard(const ITextInput& field)
{
    if (field.secure()) return false;          // passwords never go out this way
    if (!field.has_selection()) return false;

    std::string sel = field.selection_text();
    const bool ok = clipboard_backend().set_text(sel);
    crypto_wipe(sel.data(), sel.size());
    return ok;
}

bool cut_selection_to_clipboard(ITextInput& field)
{
    if (!copy_selection_to_clipboard(field)) return false;
    field.delete_selection();
    return true;
}

} // namespace ui
