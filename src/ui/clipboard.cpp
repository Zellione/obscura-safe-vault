#include "ui/clipboard.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include <SDL3/SDL.h>
#include <monocypher.h>

#include "ui/clipboard_gate.h"
#include "ui/text_input_model.h"

namespace ui {

namespace {

// The clipboard is external input. The backend contract says the buffer is
// NUL-terminated, but a bounded scan is what makes a missing terminator a
// truncation instead of a read off the end of the allocation. The cap is far
// above any field's byte cap, so it never truncates a legitimate paste.
constexpr size_t CLIPBOARD_MAX_BYTES = 1U << 20;

std::string_view bounded_view(const char* p) noexcept
{
    size_t n = 0;
    while (n < CLIPBOARD_MAX_BYTES && p[n] != '\0') ++n;
    return {p, n};
}

class SdlClipboard final : public ClipboardBackend {
public:
    char* get_text() override { return SDL_GetClipboardText(); }

    void release_text(char* p) override
    {
        if (p == nullptr) return;
        // SDL hands back a heap copy of the clipboard contents. If it held a
        // password, that copy is a second in-process exposure — wipe it before
        // freeing rather than leaving it in the allocator's free list.
        crypto_wipe(p, bounded_view(p).size());
        SDL_free(p);
    }

    bool set_text(std::string_view s) override
    {
        const std::string z(s);   // SDL needs NUL termination
        return SDL_SetClipboardText(z.c_str());
    }
};

// A function-local static rather than a namespace-scope pointer: the test seam
// needs a mutable slot, and a mutable global pointer is its own hazard.
ClipboardBackend*& override_slot() noexcept
{
    static ClipboardBackend* slot = nullptr;
    return slot;
}

} // namespace

ClipboardBackend& clipboard_backend() noexcept
{
    static SdlClipboard sdl;
    ClipboardBackend* over = override_slot();
    return over != nullptr ? *over : static_cast<ClipboardBackend&>(sdl);
}

void set_clipboard_backend(ClipboardBackend* backend) noexcept { override_slot() = backend; }

bool paste_from_clipboard(ITextInput& field)
{
    ClipboardBackend& cb = clipboard_backend();
    char* raw = cb.get_text();
    if (raw == nullptr) return false;

    // A view straight over the backend's buffer: for a secure field the bytes go
    // from here into mlock'd storage with no std::string in between.
    const std::string_view text = bounded_view(raw);
    if (text.empty()) { cb.release_text(raw); return false; }

    field.insert(text);
    cb.release_text(raw);
    return true;
}

// Apply the clipboard gate (Phase 92) to a selection already materialised as a
// plaintext string: Allow returns it for the caller to write; Refuse wipes it
// and reports no-op; Warn hands it (moved) to the gate's mlock'd pending buffer
// for the App's default-cancel confirm and reports no-op. Shared by copy and
// cut so the policy cannot drift — and so cut, whose delete must wait for an
// actual write, sees "not yet written" as a no-op instead of deleting the
// selection before the confirm lands.
[[nodiscard]] std::optional<std::string> gate_selection(std::string sel)
{
    using enum ClipboardGateAction;
    switch (clipboard_gate_action(clipboard_gate())) {
    case Refuse:
        crypto_wipe(sel.data(), sel.size());
        return std::nullopt;
    case Confirm:
        (void)request_clipboard_confirm(std::move(sel), /*sensitive=*/false);
        return std::nullopt;
    case Copy:
        return sel;
    }
    return sel;
}

bool copy_selection_to_clipboard(const ITextInput& field)
{
    if (field.secure()) return false;          // passwords never go out this way
    if (!field.has_selection()) return false;

    auto sel = gate_selection(field.selection_text());
    if (!sel) return false;
    const bool ok = clipboard_backend().set_text(*sel);
    crypto_wipe(sel->data(), sel->size());
    return ok;
}

bool cut_selection_to_clipboard(ITextInput& field)
{
    if (field.secure()) return false;          // passwords never go out this way
    if (!field.has_selection()) return false;

    // The selection is deleted only when the clipboard write actually happened:
    // under Warn the copy is parked (gate_selection returns nullopt) and the
    // text stays put until the confirm lands, under Disable it is never written.
    auto sel = gate_selection(field.selection_text());
    if (!sel) return false;
    const bool ok = clipboard_backend().set_text(*sel);
    crypto_wipe(sel->data(), sel->size());
    if (!ok) return false;
    field.delete_selection();
    return true;
}

} // namespace ui
