#include "ui/favorites_images.h"

#include <string>
#include <string_view>

#include "crypto/secure_mem.h"
#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/texture_cache.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/input.h"
#include "ui/nav_model.h"
#include "ui/widgets.h"
#include "vault/index.h"

namespace ui {

namespace {
constexpr float OX = 40;
constexpr float OY = 160;
constexpr float CELL = 188;
constexpr float GAP = 16;

GridSpec grid_spec(float win_w, int cols) noexcept
{
    const float used = static_cast<float>(cols) * CELL +
                       static_cast<float>(cols > 0 ? cols - 1 : 0) * GAP;
    const float ox = std::max(OX, (win_w - used) * 0.5f);
    return {cols, CELL, GAP, ox, OY};
}

// Parent gallery path of a full node path ("a/b/x.jpg" -> "a/b", "x.jpg" -> "").
std::string parent_path(std::string_view full_path)
{
    auto segs = split_path(full_path);
    if (segs.size() <= 1) return {};
    segs.pop_back();
    return join_path(segs);
}
}

FavoritesImages::FavoritesImages(gfx::Window& win, gfx::FontAtlas& font,
                                 vault::Vault& vault, gfx::TextureCache& cache)
    : win_(win), font_(font), vault_(vault), cache_(cache)
{
}

void FavoritesImages::on_enter()
{
    favs_ = vault_.list_favorite_images();
    nav_.set_count(static_cast<int>(favs_.size()));
    nav_.select(0);
    cols_ = grid_columns(static_cast<float>(win_.width()) - 2 * OX, CELL, GAP);
}

void FavoritesImages::open_selected()
{
    const int s = nav_.selected();
    if (s < 0 || s >= static_cast<int>(favs_.size())) return;

    // Open the viewer in the image's home gallery, positioned on it. The leaf
    // gallery holds only images, so its listing index is the viewer's index.
    const std::string home = parent_path(favs_[s].path);
    int viewer_index = 0;
    const auto siblings = vault_.list(home);
    for (size_t i = 0; i < siblings.size(); ++i) {
        if (siblings[i] == favs_[s].node) { viewer_index = static_cast<int>(i); break; }
    }
    request(NavKind::ToViewer, home, viewer_index);
}

void FavoritesImages::handle_event(const SDL_Event& e)
{
    using enum InputAction;
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
            switch (map_key(e.key.key, e.key.mod)) {
                case NavLeft:  nav_.move(-1);     break;
                case NavRight: nav_.move(1);      break;
                case NavUp:    nav_.move(-cols_); break;
                case NavDown:  nav_.move(cols_);  break;
                case Select:   open_selected();   break;
                case Back:     request(NavKind::ToGallery, "", 0); break;
                default:       break;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (const int idx = hit_test(e.button.x, e.button.y); idx >= 0) {
                nav_.select(idx);
                open_selected();
            }
            break;
        default: break;
    }
}

void FavoritesImages::update(double)
{
    if (pump_thumbs()) mark_dirty();   // off-thread thumbnail decode(s) landed
}

int FavoritesImages::hit_test(float mx, float my) const
{
    return grid_hit(mx, my, static_cast<int>(favs_.size()),
                    grid_spec(static_cast<float>(win_.width()), cols_));
}

SDL_Texture* FavoritesImages::thumb_texture(const vault::IndexNode& node)
{
    if (node.meta.thumb_length == 0) return nullptr;
    const uint64_t key = node.meta.data_offset;
    if (SDL_Texture* t = cache_.get(key)) return t;

    if (failed_.contains(key) || worker_.pending(key)) return nullptr;
    crypto::SecureBytes sb;
    if (vault_.read_thumbnail(node, sb) != vault::VaultResult::Ok) return nullptr;
    worker_.submit(key, std::move(sb));
    return nullptr;
}

bool FavoritesImages::pump_thumbs()
{
    bool any = false;
    while (auto res = worker_.take_result()) {
        if (res->image) {
            cache_.get_or_upload(res->key, *res->image);
            any = true;
        } else {
            failed_.insert(res->key);
        }
    }
    return any;
}

void FavoritesImages::render(gfx::Renderer& r)
{
    using namespace gfx::theme;
    const auto W = static_cast<float>(win_.width());

    r.draw_text(font_, OX, 40, "Favorite Images", TEXT_DIM);
    r.draw_text(font_, OX, 90, "[Enter] Open   [Esc] Back", TEXT_FAINT);

    if (favs_.empty()) {
        r.draw_text(font_, OX, OY, "No favorite images yet. Press [B] on an image "
                    "(grid or viewer) to bookmark it.", TEXT_DIM);
        return;
    }

    cols_ = grid_columns(W - 2 * OX, CELL, GAP);
    for (size_t i = 0; i < favs_.size(); ++i) {
        const SDL_FRect cellr = grid_cell_rect(static_cast<int>(i), grid_spec(W, cols_));
        const vault::IndexNode* n = favs_[i].node;
        const bool sel = (static_cast<int>(i) == nav_.selected());
        if (sel) r.draw_selection_glow(cellr, RADIUS, ACCENT);
        r.draw_round_rect(cellr, RADIUS, sel ? SURFACE_HI : SURFACE);
        r.draw_round_rect(cellr, RADIUS, sel ? ACCENT : BORDER, /*filled*/ false);

        const float ph      = font_.pixel_height();
        const float label_y = cellr.y + CELL - ph - 12.0f;
        const SDL_FRect box{cellr.x + 6, cellr.y + 6, CELL - 12, label_y - cellr.y - 12.0f};
        r.draw_rect(box, gfx::Color{0, 0, 0, 255});   // black backing, never stretched
        if (SDL_Texture* tex = thumb_texture(*n)) {
            float tw = 0;
            float th = 0;
            SDL_GetTextureSize(tex, &tw, &th);
            r.draw_image(tex, fit_rect(tw, th, box));
        } else {
            r.draw_text(font_, box.x + 6, box.y + box.h * 0.5f - 14, "(no thumb)", TEXT_DIM);
        }

        const std::string label = elide_middle(
            n->name, static_cast<int>(CELL - 16),
            [this](std::string_view s) { return font_.measure(s); });
        r.draw_text(font_, cellr.x + 8, label_y, label, TEXT);

        // Gold favorite badge, top-right (every tile here is a favorite).
        const SDL_FRect badge{cellr.x + CELL - 8 - 18, cellr.y + 8, 18, 18};
        r.draw_round_rect(badge, RADIUS_SMALL, FAVORITE);
        r.draw_round_rect(badge, RADIUS_SMALL, BG, /*filled*/ false);
    }
}

} // namespace ui
