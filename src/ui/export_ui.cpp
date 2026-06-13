#include "ui/export_ui.h"

#include "gfx/window.h"
#include "platform/folder_dialog.h"

namespace ui {

void ExportUi::begin(std::string detail) { consent_.open(std::move(detail)); }

bool ExportUi::consume_key(SDL_Keycode key)
{
    if (!consent_.active()) return false;
    if (consent_.handle_key(key) == ConsentDialog::Result::Confirmed)
        folder_.open(win_.sdl_window());
    return true;
}

bool ExportUi::modal_active() const noexcept { return consent_.active(); }

std::optional<std::filesystem::path> ExportUi::take_destination()
{
    if (auto dest = folder_.take_result(); dest && !dest->empty())
        return std::filesystem::path(*dest);
    return std::nullopt;   // empty => the picker was cancelled
}

void ExportUi::render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H) const
{
    consent_.render(r, font, W, H);
}

} // namespace ui
