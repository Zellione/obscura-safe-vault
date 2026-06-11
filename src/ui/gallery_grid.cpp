#include "ui/gallery_grid.h"

#include <algorithm>
#include <filesystem>

#include "crypto/secure_mem.h"
#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/texture_cache.h"
#include "gfx/window.h"
#include "image/decode.h"
#include "platform/file_dialog.h"
#include "platform/paths.h"
#include "ui/input.h"
#include "ui/widgets.h"
#include "vault/index.h"
#include "vault/vault.h"

namespace ui {

namespace {
constexpr float OX = 40;
constexpr float OY = 160;
constexpr float CELL = 160;
constexpr float GAP = 16;

GridSpec grid_spec(int cols) noexcept { return {cols, CELL, GAP, OX, OY}; }
}

GalleryGrid::GalleryGrid(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                         gfx::TextureCache& cache, platform::FileDialog& dlg)
    : win_(win), font_(font), vault_(vault), cache_(cache), dlg_(dlg)
{
}

void GalleryGrid::on_enter()
{
    refresh();
    // Seed cols_ from the current window size so keyboard NavUp/Down and mouse
    // hit-testing work on the first frame's events (render() recomputes it each
    // frame to track window resizes).
    cols_ = grid_columns(static_cast<float>(win_.width()) - 2 * OX, CELL, GAP);
}

void GalleryGrid::refresh()
{
    children_ = vault_.list(nav_.path());
    nav_.set_count(static_cast<int>(children_.size()));
}

bool GalleryGrid::current_allows_images() const
{
    return std::ranges::none_of(children_,
                                [](const vault::IndexNode* c) { return c->is_gallery(); });
}

bool GalleryGrid::current_allows_galleries() const
{
    return std::ranges::none_of(children_,
                                [](const vault::IndexNode* c) { return c->is_image(); });
}

void GalleryGrid::open_selected()
{
    const int s = nav_.selected();
    if (s < 0 || s >= static_cast<int>(children_.size())) return;
    const vault::IndexNode* n = children_[s];
    if (n->is_gallery()) { nav_.enter(n->name); refresh(); }
    // Image selection opens the viewer in Phase 6; no-op here.
}

void GalleryGrid::go_up()
{
    if (!nav_.up()) { vault_.lock(); request(NavKind::ToUnlock); return; }
    refresh();
}

void GalleryGrid::start_import()
{
    if (dlg_.busy()) return;
    if (!current_allows_images()) {
        error_ = "Can't import here: this gallery holds sub-galleries.";
        return;
    }
    error_.clear();
    dlg_.open_images(win_.sdl_window());
}

void GalleryGrid::do_import(const std::filesystem::path& file_path)
{
    using enum vault::VaultResult;
    auto bytes = platform::read_file(file_path);
    if (!bytes) { error_ = "Could not read " + file_path.string(); return; }

    const std::string fname = file_path.filename().string();
    switch (vault_.add_image(nav_.path(), *bytes, fname)) {
        case Ok:            break;
        case AlreadyExists: error_ = "Already exists: " + fname; break;
        case InvalidArg:    error_ = "Cannot add an image here."; break;
        default:            error_ = "Import failed: " + fname; break;
    }
}

void GalleryGrid::start_naming()
{
    if (!current_allows_galleries()) {
        error_ = "Can't create a sub-gallery in an image gallery.";
        return;
    }
    naming_ = true;
    name_buf_.clear();
    error_.clear();
    SDL_StartTextInput(win_.sdl_window());
}

void GalleryGrid::finish_naming()
{
    using enum vault::VaultResult;
    naming_ = false;
    SDL_StopTextInput(win_.sdl_window());
    if (name_buf_.empty()) return;

    const std::string base = nav_.path();
    switch (const std::string full = base.empty() ? name_buf_ : base + "/" + name_buf_;
            vault_.create_gallery(full)) {
        case Ok:            break;
        case AlreadyExists: error_ = "Gallery already exists."; break;
        case InvalidArg:    error_ = "Invalid gallery name/location."; break;
        default:            error_ = "Could not create gallery."; break;
    }
    name_buf_.clear();
    refresh();
}

SDL_Texture* GalleryGrid::thumb_texture(const vault::IndexNode& node)
{
    if (node.meta.thumb_length == 0) return nullptr;
    const uint64_t key = node.meta.data_offset;
    if (SDL_Texture* t = cache_.get(key)) return t;

    crypto::SecureBytes sb;
    if (vault_.read_thumbnail(node, sb) != vault::VaultResult::Ok) return nullptr;
    auto img = image::decode_from_memory(sb.as_span());
    if (!img) return nullptr;
    return cache_.get_or_upload(key, *img);
}

void GalleryGrid::handle_event(const SDL_Event& e)
{
    if (naming_) {
        switch (e.type) {
            case SDL_EVENT_TEXT_INPUT: name_buf_ += e.text.text; break;
            case SDL_EVENT_KEY_DOWN:
                if (e.key.key == SDLK_BACKSPACE && !name_buf_.empty())
                    name_buf_.pop_back();
                else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)
                    finish_naming();
                else if (e.key.key == SDLK_ESCAPE) {
                    naming_ = false; name_buf_.clear();
                    SDL_StopTextInput(win_.sdl_window());
                }
                break;
            default: break;
        }
        return;
    }

    switch (e.type) {
        case SDL_EVENT_KEY_DOWN: {
            using enum InputAction;
            switch (map_key(e.key.key, e.key.mod)) {
                case NavLeft:    nav_.move(-1);     break;
                case NavRight:   nav_.move(1);      break;
                case NavUp:      nav_.move(-cols_); break;
                case NavDown:    nav_.move(cols_);  break;
                case Select:     open_selected();   break;
                case Back:       go_up();           break;
                case Import:     start_import();    break;
                case NewGallery: start_naming();    break;
                default: break;
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (const int idx = grid_hit(e.button.x, e.button.y,
                                         static_cast<int>(children_.size()),
                                         grid_spec(cols_));
                idx >= 0) {
                nav_.select(idx);
                if (children_[idx]->is_gallery()) open_selected();
            }
            break;
        }
        default: break;
    }
}

void GalleryGrid::update(double)
{
    if (auto res = dlg_.take_result()) {
        if (!res->empty()) {
            for (const auto& p : *res) do_import(p);
            refresh();
        }
    }
}

void GalleryGrid::render(gfx::Renderer& r)
{
    const auto W = static_cast<float>(win_.width());
    const auto H = static_cast<float>(win_.height());
    cols_ = grid_columns(W - 2 * OX, CELL, GAP);

    std::string crumb = "/";
    for (const auto& s : nav_.segments()) { crumb += s; crumb += '/'; }
    r.draw_text(font_, OX, 40, crumb, gfx::Color{200, 200, 210, 255});
    r.draw_text(font_, OX, 90, "[I] Import   [N] New Gallery   [Enter] Open   [Esc] Up",
                gfx::Color{120, 120, 130, 255});

    for (size_t i = 0; i < children_.size(); ++i) {
        const SDL_FRect cellr = grid_cell_rect(static_cast<int>(i), grid_spec(cols_));
        const vault::IndexNode* n = children_[i];
        const bool sel = (static_cast<int>(i) == nav_.selected());
        r.draw_rect(cellr, sel ? gfx::Color{70, 70, 90, 255} : gfx::Color{45, 45, 55, 255});

        if (n->is_gallery()) {
            r.draw_rect({cellr.x + 30, cellr.y + 40, CELL - 60, CELL - 90},
                        gfx::Color{200, 170, 90, 255});
        } else if (SDL_Texture* tex = thumb_texture(*n)) {
            float tw = 0;
            float th = 0;
            SDL_GetTextureSize(tex, &tw, &th);
            r.draw_image(tex, fit_rect(tw, th, {cellr.x + 6, cellr.y + 6,
                                                CELL - 12, CELL - 40}));
        } else {
            r.draw_text(font_, cellr.x + 10, cellr.y + CELL / 2 - 14, "(no thumb)",
                        gfx::Color{150, 150, 160, 255});
        }
        r.draw_text(font_, cellr.x + 8, cellr.y + CELL - 30, n->name,
                    gfx::Color{230, 230, 235, 255});
        if (sel) r.draw_rect(cellr, gfx::Color{120, 80, 200, 255}, /*filled*/ false);
    }

    if (!error_.empty())
        r.draw_text(font_, OX, H - 36, error_, gfx::Color{230, 120, 120, 255});

    if (naming_) {
        const float mw = W * 0.6f;
        const float mh = 120;
        const float mx = (W - mw) / 2;
        const float my = (H - mh) / 2;
        r.draw_rect({mx, my, mw, mh}, gfx::Color{30, 30, 40, 255});
        r.draw_rect({mx, my, mw, mh}, gfx::Color{120, 80, 200, 255}, /*filled*/ false);
        r.draw_text(font_, mx + 16, my + 16, "New gallery name:",
                    gfx::Color{220, 220, 225, 255});
        draw_text_field(r, font_, {mx + 16, my + 56, mw - 32, 44}, name_buf_, true);
    }
}

} // namespace ui
