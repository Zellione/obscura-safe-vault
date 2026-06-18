#include "ui/vault_manager.h"

#include <utility>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "platform/vault_registry.h"
#include "ui/widgets.h"

namespace ui {

VaultManager::VaultManager(gfx::Window& win, gfx::FontAtlas& font,
                           platform::VaultRegistry& registry, platform::FileDialog& dlg,
                           std::string active_path)
    : win_(win), font_(font), registry_(registry), dlg_(dlg),
      active_path_(std::move(active_path))
{
    reload();
}

void VaultManager::on_enter() { reload(); mark_dirty(); }

void VaultManager::reload()
{
    entries_ = registry_.list();
    if (selected_ >= static_cast<int>(entries_.size()))
        selected_ = entries_.empty() ? 0 : static_cast<int>(entries_.size()) - 1;
    if (selected_ < 0) selected_ = 0;
}

VaultManager::Layout VaultManager::layout() const
{
    const auto  H   = static_cast<float>(win_.height());
    const float bw  = 220.0f;
    const float bh  = 44.0f;
    const float gap = 16.0f;
    const float row = H - 80.0f;
    return Layout{
        .new_btn  = {60.0f,             row, bw, bh},
        .open_btn = {60.0f + bw + gap,  row, bw, bh},
        .list_top = 120.0f,
        // Tall enough to contain both tile lines inside the selection box:
        // 8 (top pad) + name + path (each ~font px) + 8 (bottom pad).
        .row_h    = 84.0f,
    };
}

void VaultManager::move(int delta)
{
    if (entries_.empty()) return;
    selected_ += delta;
    if (selected_ < 0) selected_ = 0;
    const int last = static_cast<int>(entries_.size()) - 1;
    if (selected_ > last) selected_ = last;
}

void VaultManager::open_selected()
{
    if (entries_.empty() || selected_ < 0 || selected_ >= static_cast<int>(entries_.size()))
        return;
    const std::string path = entries_[static_cast<size_t>(selected_)].string();
    if (!active_path_.empty() && path == active_path_)
        request(NavKind::ToGallery);            // already unlocked: jump straight in
    else
        request(NavKind::ToUnlock, path);       // App locks the old active, unlocks this
}

void VaultManager::remove_selected()
{
    if (entries_.empty() || selected_ < 0 || selected_ >= static_cast<int>(entries_.size()))
        return;
    (void)registry_.remove(entries_[static_cast<size_t>(selected_)]);
    reload();
    mark_dirty();
}

void VaultManager::handle_key(const SDL_KeyboardEvent& key)
{
    switch (key.key) {
        case SDLK_UP:     move(-1); mark_dirty(); break;
        case SDLK_DOWN:   move(+1); mark_dirty(); break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:  open_selected(); break;
        case SDLK_N:      dlg_.save_vault(win_.sdl_window()); awaiting_dialog_ = true; break;
        case SDLK_O:      dlg_.open_vault(win_.sdl_window()); awaiting_dialog_ = true; break;
        case SDLK_R:
        case SDLK_DELETE: remove_selected(); break;
        case SDLK_L:      if (!active_path_.empty()) request(NavKind::LockActive); break;
        case SDLK_ESCAPE:
        case SDLK_Q:      request(NavKind::Quit); break;
        default: break;
    }
}

int VaultManager::hit_test(float my) const
{
    const Layout L = layout();
    if (my < L.list_top) return -1;
    const auto idx = static_cast<int>((my - L.list_top) / L.row_h);
    return (idx >= 0 && idx < static_cast<int>(entries_.size())) ? idx : -1;
}

void VaultManager::handle_click(const SDL_MouseButtonEvent& b)
{
    mouse_down_ = (b.button == SDL_BUTTON_LEFT);
    mouse_x_ = b.x; mouse_y_ = b.y;
    if (b.button != SDL_BUTTON_LEFT) return;

    const Layout L = layout();
    if (point_in_rect(b.x, b.y, L.new_btn)) {
        dlg_.save_vault(win_.sdl_window()); awaiting_dialog_ = true; return;
    }
    if (point_in_rect(b.x, b.y, L.open_btn)) {
        dlg_.open_vault(win_.sdl_window()); awaiting_dialog_ = true; return;
    }
    if (const int row = hit_test(b.y); row >= 0) {
        selected_ = row;
        open_selected();
    }
}

void VaultManager::handle_event(const SDL_Event& e)
{
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:          handle_key(e.key); break;
        case SDL_EVENT_MOUSE_MOTION:      mouse_x_ = e.motion.x; mouse_y_ = e.motion.y; break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: handle_click(e.button); break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) mouse_down_ = false;
            break;
        default: break;
    }
}

void VaultManager::update(double)
{
    // A New/Open file dialog resolves asynchronously; pick up its result here.
    // Both routes lead to the unlock screen, which auto-selects create-vs-open by
    // whether the chosen path already exists.
    if (!awaiting_dialog_) return;
    auto res = dlg_.take_result();
    if (!res) return;                            // still open
    awaiting_dialog_ = false;
    if (res->empty()) { mark_dirty(); return; }  // cancelled
    request(NavKind::ToUnlock, (*res)[0]);
}

void VaultManager::render(gfx::Renderer& r)
{
    using namespace gfx::theme;
    const auto W = static_cast<float>(win_.width());
    const Layout L = layout();

    r.draw_text(font_, 60.0f, 44.0f, "Vaults", TEXT);

    if (entries_.empty()) {
        r.draw_text(font_, 60.0f, L.list_top, "No vaults yet — press N to create, O to open.",
                    TEXT_DIM);
    }

    const float row_pad = 12.0f;
    for (size_t i = 0; i < entries_.size(); ++i) {
        const SDL_FRect box{40.0f, L.list_top + static_cast<float>(i) * L.row_h,
                            W - 80.0f, L.row_h - row_pad};
        const bool sel = (static_cast<int>(i) == selected_);
        if (sel) r.draw_selection_glow(box, RADIUS, ACCENT);
        r.draw_round_rect(box, RADIUS, sel ? SURFACE_HI : SURFACE);

        const std::string full = entries_[i].string();
        const std::string label = entries_[i].filename().string();
        r.draw_text(font_, box.x + 18.0f, box.y + 8.0f, label, TEXT);

        auto measure = [this](std::string_view s) { return font_.measure(s); };
        const std::string shown = elide_middle(full, static_cast<int>(box.w - 220.0f), measure);
        r.draw_text(font_, box.x + 18.0f, box.y + 8.0f + font_.pixel_height(), shown, TEXT_DIM);

        if (!active_path_.empty() && full == active_path_)
            r.draw_text(font_, box.x + box.w - 130.0f, box.y + 14.0f, "unlocked", OK);
    }

    auto btn = [this, &r](const SDL_FRect& rect, std::string_view text) {
        const ButtonState s = button_state(rect, mouse_x_, mouse_y_, mouse_down_);
        draw_button(r, font_, {rect, std::string(text)}, s.hover, s.active);
    };
    btn(L.new_btn, "New Vault (N)");
    btn(L.open_btn, "Open Other (O)");
}

} // namespace ui
