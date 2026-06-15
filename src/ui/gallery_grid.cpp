#include "ui/gallery_grid.h"

#include <algorithm>
#include <filesystem>
#include <format>

#include "crypto/secure_mem.h"
#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/texture_cache.h"
#include "gfx/theme.h"
#include "ui/meta_format.h"
#include "gfx/window.h"
#include "image/decode.h"
#include "platform/file_dialog.h"
#include "platform/folder_dialog.h"
#include "platform/paths.h"
#include "ui/export.h"
#include "ui/input.h"
#include "ui/widgets.h"
#include "vault/index.h"
#include "vault/vault.h"

namespace ui {

namespace {
constexpr float OX = 40;
constexpr float OY = 160;
constexpr float CELL = 188;     // tile side (slightly larger thumbnails)
constexpr float GAP = 16;
constexpr float ROW_H = 44;     // detailed-list row pitch (very small thumbnails)
constexpr float LIST_HEADER = 30;  // column-header band above the list rows
// Right-anchored metadata column widths (px) for the detailed list view.
constexpr float COL_DIMS = 165;   // wide enough for the "DIMENSIONS" header
constexpr float COL_SIZE = 100;
constexpr float COL_TYPE = 70;
constexpr float COL_DATE = 120;

// Centre the `cols` columns horizontally in a `win_w`-wide window so the left and
// right margins match (never tighter than OX).
GridSpec grid_spec(float win_w, int cols) noexcept
{
    const float used = static_cast<float>(cols) * CELL +
                       static_cast<float>(cols > 0 ? cols - 1 : 0) * GAP;
    const float ox = std::max(OX, (win_w - used) * 0.5f);
    return {cols, CELL, GAP, ox, OY};
}
}

GalleryGrid::GalleryGrid(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                         gfx::TextureCache& cache, platform::FileDialog& dlg,
                         platform::FolderDialog& folder_dlg, GridLocation at)
    : win_(win), font_(font), vault_(vault), cache_(cache), dlg_(dlg),
      folder_dlg_(folder_dlg), search_(vault, win), tag_editor_(vault, win),
      initial_path_(std::move(at.path)), initial_sel_(at.selected)
{
}

void GalleryGrid::on_enter()
{
    // Restore the saved location (e.g. when returning from the image viewer).
    for (const auto& seg : split_path(initial_path_)) nav_.enter(seg);
    refresh();
    nav_.select(initial_sel_);
    // Seed cols_ from the current window size so keyboard NavUp/Down and mouse
    // hit-testing work on the first frame's events (render() recomputes it each
    // frame to track window resizes).
    cols_ = grid_columns(static_cast<float>(win_.width()) - 2 * OX, CELL, GAP);
}

void GalleryGrid::refresh()
{
    children_ = vault_.list(nav_.path());
    nav_.set_count(static_cast<int>(children_.size()));
    sel_.clear();   // selection indices are only valid against the current listing
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
    else                 request(NavKind::ToViewer, nav_.path(), s);
}

void GalleryGrid::go_up()
{
    if (!nav_.up()) { vault_.lock(); request(NavKind::ToUnlock); return; }
    refresh();
}

void GalleryGrid::toggle_select()
{
    const int s = nav_.selected();
    if (s < 0 || s >= static_cast<int>(children_.size())) return;
    if (!children_[s]->is_image()) return;   // only images are exportable
    sel_.toggle(s);
    status_.clear();
}

void GalleryGrid::start_export()
{
    if (folder_dlg_.busy() || consent_.active()) return;
    if (sel_.empty()) {
        error_ = "Select images first (Space), then [X] to export.";
        return;
    }
    error_.clear();
    status_.clear();
    const std::size_t n = sel_.count();
    consent_.open(std::format("Export {} {}", n, n == 1 ? "image?" : "images?"));
}

void GalleryGrid::do_export(const std::filesystem::path& dest)
{
    std::vector<const vault::IndexNode*> picked;
    for (int idx : sel_.indices())
        if (idx >= 0 && idx < static_cast<int>(children_.size()))
            picked.push_back(children_[idx]);

    const ui::ExportSummary sum =
        ui::export_images(vault_, picked, dest, ui::ExportConsent::Confirm);

    if (sum.failed > 0)
        error_ = std::format("Export: {} failed.", sum.failed);
    status_ = std::format("Exported {} image{} to {}", sum.written,
                          sum.written == 1 ? "" : "s", dest.string());
    sel_.clear();
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

void GalleryGrid::start_search()
{
    search_.open();
}

void GalleryGrid::start_tag_editor()
{
    const int s = nav_.selected();
    if (s < 0 || s >= static_cast<int>(children_.size())) return;

    const vault::IndexNode* n = children_[s];
    const std::string base = nav_.path();
    const std::string full_path = base.empty() ? n->name : base + "/" + n->name;
    tag_editor_.open(full_path);
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

void GalleryGrid::handle_naming_key(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_TEXT_INPUT) { name_buf_ += e.text.text; return; }
    if (e.type != SDL_EVENT_KEY_DOWN) return;
    if (e.key.key == SDLK_BACKSPACE && !name_buf_.empty())
        name_buf_.pop_back();
    else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)
        finish_naming();
    else if (e.key.key == SDLK_ESCAPE) {
        naming_ = false; name_buf_.clear();
        SDL_StopTextInput(win_.sdl_window());
    }
}

void GalleryGrid::toggle_or_open()
{
    if (const int s = nav_.selected();
        s >= 0 && s < static_cast<int>(children_.size()) && children_[s]->is_image())
        toggle_select();
    else
        open_selected();
}

void GalleryGrid::handle_key_down(const SDL_KeyboardEvent& key)
{
    using enum GalleryView;
    if (key.key == SDLK_L) { view_ = (view_ == Grid) ? List : Grid; return; }
    if (key.key == SDLK_X) { start_export(); return; }   // export selection
    if (key.key == SDLK_SPACE) { toggle_or_open(); return; }
    if (key.key == SDLK_SLASH) { start_search(); return; }  // search (/)
    if (key.key == SDLK_G) { start_tag_editor(); return; }  // tag editor (G)

    using enum InputAction;
    switch (map_key(key.key, key.mod)) {
        case NavLeft:    nav_.move(-1);     break;
        case NavRight:   nav_.move(1);      break;
        case NavUp:      nav_.move(-cols_); break;
        case NavDown:    nav_.move(cols_);  break;
        case Select:     open_selected();   break;
        case Back:
            if (!sel_.empty()) { sel_.clear(); status_.clear(); }
            else               go_up();
            break;
        case Import:     start_import();    break;
        case NewGallery: start_naming();    break;
        default: break;
    }
}

void GalleryGrid::handle_event(const SDL_Event& e)
{
    // Overlays take input in priority order: search > tag_editor > consent/naming
    if (search_.active()) {
        if (search_.handle_event(e)) {
            Nav nav = search_.take_nav();
            if (nav.kind != NavKind::None) request(nav.kind, std::move(nav.path), nav.index);
        }
        return;
    }

    if (tag_editor_.active()) {
        (void)tag_editor_.handle_event(e);
        return;
    }

    // The export consent modal owns all input while it is up.
    if (consent_.active()) {
        if (e.type == SDL_EVENT_KEY_DOWN &&
            consent_.handle_key(e.key.key) == ConsentDialog::Result::Confirmed)
            folder_dlg_.open(win_.sdl_window());
        return;
    }

    if (naming_) { handle_naming_key(e); return; }

    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
            handle_key_down(e.key);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (const int idx = hit_test(e.button.x, e.button.y); idx >= 0) {
                nav_.select(idx);
                open_selected();   // gallery → descend; image → open viewer
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
    if (auto dest = folder_dlg_.take_result()) {
        if (!dest->empty()) do_export(*dest);   // empty => the picker was cancelled
    }
}

void GalleryGrid::render(gfx::Renderer& r)
{
    const auto W = static_cast<float>(win_.width());
    const auto H = static_cast<float>(win_.height());

    using namespace gfx::theme;
    std::string crumb = "/";
    for (const auto& s : nav_.segments()) { crumb += s; crumb += '/'; }
    r.draw_text(font_, OX, 40, crumb, TEXT_DIM);
    r.draw_text(font_, OX, 90,
                "[I] Import  [N] New  [/] Search  [G] Tags  [Enter] Open  [Space] Select  "
                "[X] Export  [Esc] Back  [L] List/Grid",
                TEXT_FAINT);

    if (!sel_.empty())
        r.draw_text(font_, OX, 120, std::format("{} selected", sel_.count()), ACCENT);

    if (view_ == GalleryView::List) render_list(r, W, H);
    else                            render_grid(r, W, H);

    if (!error_.empty())
        r.draw_text(font_, OX, H - 36, error_, DANGER);
    else if (!status_.empty())
        r.draw_text(font_, OX, H - 36, status_, OK);

    consent_.render(r, font_, W, H);

    if (naming_) {
        const float mw = W * 0.6f;
        const float mh = 120;
        const float mx = (W - mw) / 2;
        const float my = (H - mh) / 2;
        r.draw_round_rect({mx, my, mw, mh}, RADIUS, SURFACE);
        r.draw_round_rect({mx, my, mw, mh}, RADIUS, ACCENT, /*filled*/ false);
        r.draw_text(font_, mx + 16, my + 16, "New gallery name:", TEXT);
        draw_text_field(r, font_, {mx + 16, my + 56, mw - 32, 44}, name_buf_, true);
    }

    search_.render(r, font_, W, H);
    tag_editor_.render(r, font_, W, H);
}

void GalleryGrid::render_grid(gfx::Renderer& r, float W, float /*H*/)
{
    using namespace gfx::theme;
    cols_ = grid_columns(W - 2 * OX, CELL, GAP);
    for (size_t i = 0; i < children_.size(); ++i) {
        const SDL_FRect cellr = grid_cell_rect(static_cast<int>(i), grid_spec(W, cols_));
        const vault::IndexNode* n = children_[i];
        const bool sel = (static_cast<int>(i) == nav_.selected());
        if (sel) r.draw_selection_glow(cellr, RADIUS, ACCENT);
        r.draw_round_rect(cellr, RADIUS, sel ? SURFACE_HI : SURFACE);
        r.draw_round_rect(cellr, RADIUS, sel ? ACCENT : BORDER, /*filled*/ false);

        // Leave a 12px gap below the label so it never touches the tile border.
        const float ph      = font_.pixel_height();
        const float label_y = cellr.y + CELL - ph - 12.0f;
        draw_tile_thumb(r, *n, {cellr.x + 6, cellr.y + 6,
                                CELL - 12, label_y - cellr.y - 12.0f});
        r.draw_text(font_, cellr.x + 8, label_y, fit_name(n->name, CELL - 16), TEXT);

        // Export-selection badge: a small accent square in the top-left corner.
        if (sel_.contains(static_cast<int>(i))) {
            const SDL_FRect badge{cellr.x + 8, cellr.y + 8, 18, 18};
            r.draw_round_rect(badge, RADIUS_SMALL, ACCENT);
            r.draw_round_rect(badge, RADIUS_SMALL, BG, /*filled*/ false);
        }
    }
}

void GalleryGrid::render_list(gfx::Renderer& r, float W, float /*H*/)
{
    using namespace gfx::theme;
    cols_ = 1;   // up/down move one row at a time
    const float rw    = W - 2 * OX;
    const float right = OX + rw;

    // Right-anchored metadata columns; the name column flexes to fill the rest.
    const float dims_x = right - (COL_DIMS + COL_SIZE + COL_TYPE + COL_DATE);
    const float size_x = dims_x + COL_DIMS;
    const float type_x = size_x + COL_SIZE;
    const float date_x = type_x + COL_TYPE;

    // Column header + separator.
    const float hy = font_.text_top_for_center(OY + (LIST_HEADER - 6) * 0.5f);
    r.draw_text(font_, OX, hy, "NAME", TEXT_FAINT);
    r.draw_text(font_, dims_x, hy, "DIMENSIONS", TEXT_FAINT);
    r.draw_text(font_, size_x, hy, "SIZE", TEXT_FAINT);
    r.draw_text(font_, type_x, hy, "TYPE", TEXT_FAINT);
    r.draw_text(font_, date_x, hy, "DATE", TEXT_FAINT);
    r.draw_rect({OX, OY + LIST_HEADER - 6, rw, 1.0f}, BORDER);

    for (size_t i = 0; i < children_.size(); ++i) {
        const vault::IndexNode* n = children_[i];
        const auto& m  = n->meta;
        const bool sel = (static_cast<int>(i) == nav_.selected());
        const SDL_FRect row{OX, OY + LIST_HEADER + static_cast<float>(i) * ROW_H,
                            rw, ROW_H - 6};
        if (sel) {
            r.draw_round_rect(row, RADIUS_SMALL, SURFACE_HI);
            r.draw_round_rect(row, RADIUS_SMALL, ACCENT, /*filled*/ false);
        }
        // Export-selection marker: an accent bar down the row's left edge.
        if (sel_.contains(static_cast<int>(i)))
            r.draw_rect({row.x, row.y, 4, row.h}, ACCENT);

        const SDL_FRect thumb{row.x + 5, row.y + 5, row.h - 10, row.h - 10};
        draw_tile_thumb(r, *n, thumb);

        const float ty = font_.text_top_for_center(row.y + row.h * 0.5f);  // vertically centred
        const float nx = thumb.x + thumb.w + 12;
        r.draw_text(font_, nx, ty, fit_name(n->name, dims_x - nx - 10),
                    sel ? TEXT : TEXT_DIM);

        // Galleries have no image metadata; show a folder marker instead.
        const gfx::Color meta_c = sel ? TEXT : TEXT_DIM;
        if (n->is_gallery()) {
            r.draw_text(font_, dims_x, ty, "-", meta_c);
            r.draw_text(font_, size_x, ty, "-", meta_c);
            r.draw_text(font_, type_x, ty, "DIR", meta_c);
            r.draw_text(font_, date_x, ty, "-", meta_c);
        } else {
            r.draw_text(font_, dims_x, ty, format_dimensions(m.width, m.height), meta_c);
            r.draw_text(font_, size_x, ty, format_size(m.orig_size), meta_c);
            r.draw_text(font_, type_x, ty, image_format_name(m.format), meta_c);
            r.draw_text(font_, date_x, ty, format_date(m.created_ts), meta_c);
        }
    }
}

void GalleryGrid::draw_tile_thumb(gfx::Renderer& r, const vault::IndexNode& n,
                                  const SDL_FRect& box)
{
    using namespace gfx::theme;
    if (n.is_gallery()) {
        const float ix = box.w * 0.18f;
        r.draw_round_rect({box.x + ix, box.y + box.h * 0.28f,
                           box.w - 2 * ix, box.h * 0.48f}, RADIUS_SMALL, FOLDER);
        return;
    }
    r.draw_rect(box, gfx::Color{0, 0, 0, 255});   // black backing, never stretched
    if (SDL_Texture* tex = thumb_texture(n)) {
        float tw = 0;
        float th = 0;
        SDL_GetTextureSize(tex, &tw, &th);
        r.draw_image(tex, fit_rect(tw, th, box));
    } else {
        r.draw_text(font_, box.x + 6, box.y + box.h * 0.5f - 14, "(no thumb)", TEXT_DIM);
    }
}

int GalleryGrid::hit_test(float mx, float my) const
{
    const auto count = static_cast<int>(children_.size());
    if (view_ == GalleryView::List) {
        const float top = OY + LIST_HEADER;
        if (mx < OX || mx > static_cast<float>(win_.width()) - OX || my < top) return -1;
        const auto idx = static_cast<int>((my - top) / ROW_H);
        return (idx >= 0 && idx < count) ? idx : -1;
    }
    return grid_hit(mx, my, count, grid_spec(static_cast<float>(win_.width()), cols_));
}

std::string GalleryGrid::fit_name(std::string_view name, float max_w) const
{
    return elide_middle(name, static_cast<int>(max_w),
                        [this](std::string_view s) { return font_.measure(s); });
}

} // namespace ui
