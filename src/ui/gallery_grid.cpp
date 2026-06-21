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
#include "platform/file_dialog.h"
#include "platform/folder_dialog.h"
#include "platform/paths.h"
#include "ui/delete_summary.h"
#include "ui/export.h"
#include "ui/input.h"
#include "ui/widgets.h"
#include "ui/zip_import.h"
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
constexpr float COL_TYPE = 90;    // fits image formats and "H.264"/"H.265"
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
                         gfx::TextureCache& cache, GridDialogs dialogs,
                         GridVaultCtx vault_ctx, GridLocation at)
    : win_(win), font_(font), vault_(vault), cache_(cache), dialogs_(dialogs),
      search_(vault, win), tag_editor_(vault, win),
      quick_switch_(vault_ctx.registry, vault_ctx.active_vault_path),
      transfer_(vault, std::move(vault_ctx.active_vault_path), vault_ctx.registry,
                dialogs.file, win),
      initial_(std::move(at))
{
}

void GalleryGrid::on_enter()
{
    // Restore the saved location (e.g. when returning from the image viewer).
    for (const auto& seg : split_path(initial_.path)) nav_.enter(seg);
    refresh();
    nav_.select(initial_.selected);
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
    // A leaf that holds media (images OR videos) can't also hold sub-galleries.
    return std::ranges::none_of(children_,
                                [](const vault::IndexNode* c) { return c->is_media(); });
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
    if (!nav_.up()) { request(NavKind::ToVaultManager); return; }
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
    if (dialogs_.folder.busy() || consent_.active()) return;
    if (sel_.empty()) {
        error_ = "Select images first (Space), then [X] to export.";
        return;
    }
    error_.clear();
    status_.clear();
    const std::size_t n = sel_.count();
    consent_.open(std::format("Export {} {}", n, n == 1 ? "image?" : "images?"));
}

void GalleryGrid::start_transfer()
{
    if (transfer_.active()) return;

    // Images selected (Space) → move them. Otherwise, the focused tile: a gallery
    // moves the whole subtree; a lone image moves just that image.
    if (sel_.empty()) {
        const int s = nav_.selected();
        if (s < 0 || s >= static_cast<int>(children_.size())) {
            error_ = "Nothing to move.";
            return;
        }
        const vault::IndexNode* node = children_[s];
        error_.clear();
        if (node->is_gallery()) {
            const std::string path = nav_.path().empty() ? node->name
                                                          : nav_.path() + "/" + node->name;
            transfer_.open_gallery(path);
        } else {
            transfer_.open(nav_.path(), {node->name});
        }
        return;
    }

    std::vector<std::string> names;
    for (int idx : sel_.indices())
        if (idx >= 0 && idx < static_cast<int>(children_.size()) && children_[idx]->is_image())
            names.push_back(children_[idx]->name);
    if (names.empty()) { error_ = "Select images (not galleries) to move."; return; }
    error_.clear();
    transfer_.open(nav_.path(), std::move(names));
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
    if (dialogs_.file.busy()) return;
    if (!current_allows_images()) {
        error_ = "Can't import here: this gallery holds sub-galleries.";
        return;
    }
    error_.clear();
    dialogs_.file.open_images(win_.sdl_window());
}

void GalleryGrid::do_import(const std::filesystem::path& file_path)
{
    using enum vault::VaultResult;
    auto bytes = platform::read_file(file_path);
    if (!bytes) { error_ = "Could not read " + file_path.string(); return; }

    const std::string fname = file_path.filename().string();
    const bool video = is_video_filename(fname);
    const vault::VaultResult res = video ? vault_.add_video(nav_.path(), *bytes, fname)
                                         : vault_.add_image(nav_.path(), *bytes, fname);
    switch (res) {
        case Ok:            break;
        case AlreadyExists: error_ = "Already exists: " + fname; break;
        case InvalidArg:    error_ = video ? "Unsupported or corrupt video: " + fname
                                           : "Cannot add media here."; break;
        default:            error_ = "Import failed: " + fname; break;
    }
}

void GalleryGrid::start_naming()
{
    if (!current_allows_galleries()) {
        error_ = "Can't create a sub-gallery in an image gallery.";
        return;
    }
    naming_.active = true;
    naming_.buf.clear();
    error_.clear();
    SDL_StartTextInput(win_.sdl_window());
}

void GalleryGrid::finish_naming()
{
    naming_.active = false;
    SDL_StopTextInput(win_.sdl_window());
    if (naming_.buf.empty()) {
        naming_.zip.active = false;
        return;
    }

    // If this is a zip import, trigger the import with the chosen name.
    if (naming_.zip.active) {
        naming_.zip.gallery_name = naming_.buf;
        naming_.buf.clear();
        do_zip_import(naming_.zip.path, ui::ZipConflictPolicy::AskUser);
        return;
    }

    // Otherwise, create a new gallery.
    using enum vault::VaultResult;
    const std::string base = nav_.path();
    switch (const std::string full = base.empty() ? naming_.buf : base + "/" + naming_.buf;
            vault_.create_gallery(full)) {
        case Ok:            break;
        case AlreadyExists: error_ = "Gallery already exists."; break;
        case InvalidArg:    error_ = "Invalid gallery name/location."; break;
        default:            error_ = "Could not create gallery."; break;
    }
    naming_.buf.clear();
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

void GalleryGrid::toggle_favorite_current()
{
    const int s = nav_.selected();
    if (s < 0 || s >= static_cast<int>(children_.size())) return;

    const vault::IndexNode* n = children_[s];
    const std::string base = nav_.path();
    const std::string full_path = base.empty() ? n->name : base + "/" + n->name;
    // The flag flips on the same in-memory node children_[s] points at, so the
    // star badge re-renders next frame; the key event already triggers a repaint.
    // No refresh() — that would needlessly clear the export selection.
    (void)vault_.toggle_favorite(full_path);
}

SDL_Texture* GalleryGrid::thumb_texture(const vault::IndexNode& node)
{
    if (node.meta.thumb_length == 0) return nullptr;
    const uint64_t key = node.meta.data_offset;
    if (SDL_Texture* t = cache_.get(key)) return t;

    // A thumbnail that already failed to decode is not retried; an in-flight
    // decode lands via pump_thumbs(). Otherwise read+decrypt here (fast) and
    // enqueue the slow decode off-thread, drawing a placeholder until it lands.
    if (thumbs_.failed.contains(key) || thumbs_.worker.pending(key)) return nullptr;
    crypto::SecureBytes sb;
    if (vault_.read_thumbnail(node, sb) != vault::VaultResult::Ok) return nullptr;
    thumbs_.worker.submit(key, std::move(sb));
    return nullptr;
}

bool GalleryGrid::pump_thumbs()
{
    bool any = false;
    while (auto res = thumbs_.worker.take_result()) {
        if (res->image) {
            cache_.get_or_upload(res->key, *res->image);
            any = true;
        } else {
            thumbs_.failed.insert(res->key);
        }
    }
    return any;
}

void GalleryGrid::handle_naming_key(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_TEXT_INPUT) { naming_.buf += e.text.text; return; }
    if (e.type != SDL_EVENT_KEY_DOWN) return;
    if (e.key.key == SDLK_BACKSPACE && !naming_.buf.empty())
        naming_.buf.pop_back();
    else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)
        finish_naming();
    else if (e.key.key == SDLK_ESCAPE) {
        naming_.active = false;
        naming_.buf.clear();
        naming_.zip.active = false;   // clear zip import state if cancelled
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
    if (key.key == SDLK_GRAVE) { quick_switch_.open(); return; }   // switch vault (`)
    if (key.key == SDLK_L) { view_ = (view_ == Grid) ? List : Grid; return; }
    if (key.key == SDLK_X) { start_export(); return; }   // export selection
    if (key.key == SDLK_M) { start_transfer(); return; }   // move to another vault
    if (key.key == SDLK_Z) {   // import zip archive (inlined to keep GalleryGrid <= 35 methods)
        if (dialogs_.file.busy() || transfer_.active()) return;
        error_.clear();
        dialogs_.file.open_zip(win_.sdl_window());
        return;
    }
    if (key.key == SDLK_DELETE) {   // delete the focused image/video/gallery (confirm first)
        if (const int s = nav_.selected();
            s >= 0 && s < static_cast<int>(children_.size())) {
            error_.clear();
            naming_.confirm_delete = true;
        } else {
            error_ = "Nothing selected to delete.";
        }
        return;
    }
    if (key.key == SDLK_SPACE) { toggle_or_open(); return; }
    // `/` is a shifted key on many non-US layouts, so the base keycode (key.key)
    // is the unmodified symbol (e.g. '7') and never equals SDLK_SLASH. Resolve the
    // character the layout + held modifiers actually produce and match that.
    if (SDL_GetKeyFromScancode(key.scancode, key.mod, false) == SDLK_SLASH) {
        start_search();  // search (/)
        return;
    }
    if (key.key == SDLK_G) { start_tag_editor(); return; }  // tag editor (G)
    if (key.key == SDLK_B) { toggle_favorite_current(); return; }  // favorite (B)
    if (key.key == SDLK_F) {  // open a favorites screen (Shift+F = galleries)
        request((key.mod & SDL_KMOD_SHIFT) ? NavKind::ToFavoriteGalleries
                                           : NavKind::ToFavoriteImages);
        return;
    }

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

bool GalleryGrid::handle_zip_conflict_key(const SDL_Event& e)
{
    if (!naming_.zip.active || naming_.active || e.type != SDL_EVENT_KEY_DOWN) return false;
    switch (e.key.key) {
        case SDLK_F:  // flatten all mixed folders
            do_zip_import(naming_.zip.path, ui::ZipConflictPolicy::FlattenMixed);
            mark_dirty();
            return true;
        case SDLK_S:  // skip directories with mixed content
            do_zip_import(naming_.zip.path, ui::ZipConflictPolicy::SkipMixed);
            mark_dirty();
            return true;
        case SDLK_ESCAPE:  // cancel the import
            naming_.zip.active = false;
            mark_dirty();
            return true;
        default:
            return false;
    }
}

void GalleryGrid::handle_event(const SDL_Event& e)
{
    // Overlays take input in priority order: search > tag_editor > transfer > consent/naming
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

    if (transfer_.active()) { (void)transfer_.handle_event(e); return; }

    if (quick_switch_.active()) {
        (void)quick_switch_.handle_event(e);
        if (std::string p; quick_switch_.consume_choice(p))
            request(NavKind::ToUnlock, std::move(p));   // locks current, unlocks chosen
        return;
    }

    // The export consent modal owns all input while it is up.
    if (consent_.active()) {
        if (e.type == SDL_EVENT_KEY_DOWN &&
            consent_.handle_key(e.key.key) == ConsentDialog::Result::Confirmed)
            dialogs_.folder.open(win_.sdl_window());
        return;
    }

    // The delete-confirmation modal owns all input while it is up. Y deletes the
    // focused media tile; Esc / N cancel; every other key is swallowed.
    if (naming_.confirm_delete) {
        if (e.type == SDL_EVENT_KEY_DOWN) {
            if (e.key.key == SDLK_Y) {
                if (const int s = nav_.selected();
                    s >= 0 && s < static_cast<int>(children_.size())) {
                    const vault::IndexNode* n = children_[s];
                    const std::string name = n->name;
                    const std::string base = nav_.path();
                    const vault::VaultResult r =
                        n->is_gallery()
                            ? vault_.remove_gallery(base.empty() ? name : base + "/" + name)
                            : vault_.remove_image(base, name);
                    if (r == vault::VaultResult::Ok) {
                        status_ = "Deleted " + name;
                        refresh();
                    } else {
                        error_ = "Could not delete " + name;
                    }
                }
                naming_.confirm_delete = false;
                mark_dirty();
            } else if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_N) {
                naming_.confirm_delete = false;
                mark_dirty();
            }
        }
        return;
    }

    // The zip conflict modal owns all input while it is up (only when not naming).
    if (handle_zip_conflict_key(e)) return;

    if (naming_.active) { handle_naming_key(e); return; }

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

void GalleryGrid::pump_import()
{
    if (auto res = dialogs_.file.take_result(platform::FileDialog::Purpose::Images)) {
        if (!res->empty()) {
            for (const auto& p : *res) do_import(p);
            refresh();
        }
        mark_dirty();   // dialog closed (imported or cancelled) — repaint
    }
}

void GalleryGrid::pump_zip_import()
{
    if (auto res = dialogs_.file.take_result(platform::FileDialog::Purpose::Zip)) {
        if (!res->empty()) {
            const std::string zip_path = res->front();
            const std::filesystem::path zp(zip_path);

            // Decide between Append (if current holds media) or NewGallery (name prompt).
            if (current_allows_images() && !current_allows_galleries()) {
                // Current gallery holds only media (leaf): import with Append (no name prompt).
                naming_.zip.path = zp;
                naming_.zip.gallery_name.clear();
                naming_.zip.dest = ui::ZipDest::Append;
                naming_.zip.active = true;
                do_zip_import(zp, ui::ZipConflictPolicy::AskUser);
            } else {
                // Current is empty or holds sub-galleries: prompt for new gallery name.
                start_naming();   // reuse the naming flow
                naming_.buf = zp.stem().string();   // e.g. "archive" from "archive.zip"
                naming_.zip.path = zp;
                naming_.zip.dest = ui::ZipDest::NewGallery;
                naming_.zip.active = true;
            }
        }
        mark_dirty();   // dialog closed (picked or cancelled) — repaint
    }
}

void GalleryGrid::do_zip_import(const std::filesystem::path& zip_path, ui::ZipConflictPolicy policy)
{
    // The gallery name and destination come from naming_.zip.
    const std::string gallery_name = naming_.zip.gallery_name;
    const std::string base_gallery = nav_.path();
    const ui::ZipDest dest = naming_.zip.dest;

    // Call the import_zip executor.
    const ui::ZipImportOutcome out = ui::import_zip(
        vault_, zip_path,
        /* dest */ dest,
        /* base_gallery */ base_gallery,
        /* new_gallery_name */ gallery_name,
        /* policy */ policy
    );

    if (!out.ok) {
        error_ = out.error.empty() ? "ZIP import failed." : out.error;
        naming_.zip.active = false;
        return;
    }

    // If there are mixed folders and we're in AskUser mode, show the conflict modal.
    if (out.needs_resolution) {
        // pending_zip is already stashed from pump_zip_import; the modal will be
        // drawn in render() and the user will choose FlattenMixed or SkipMixed.
        // We'll re-call do_zip_import with the chosen policy from the modal.
        return;
    }

    // Success: set the summary and refresh.
    status_ = std::format("Imported {} file{}, skipped {}", out.imported,
                         out.imported == 1 ? "" : "s", out.skipped);
    naming_.zip.active = false;
    refresh();
}

void GalleryGrid::update(double)
{
    if (pump_thumbs()) mark_dirty();   // off-thread thumbnail decode(s) landed

    if (transfer_.active()) {
        transfer_.update();            // poll the keyfile picker while the dialog is open
        mark_dirty();
    }

    // A finished transfer closes the dialog synchronously (active_ -> false) during
    // the keypress, so its result MUST be drained regardless of active state — else
    // the listing only refreshes after leaving and re-entering the gallery.
    if (std::string s; transfer_.consume_completed(s)) {
        status_ = std::move(s);
        sel_.clear();
        refresh();                     // moved images are gone from this listing
        mark_dirty();
    }

    // The import picker shares dialogs_.file with the transfer's keyfile picker, so
    // only poll it when no transfer is active (don't steal the keyfile result).
    if (!transfer_.active()) {
        pump_import();
        pump_zip_import();
    }

    if (auto dest = dialogs_.folder.take_result()) {
        if (!dest->empty()) do_export(*dest);   // empty => the picker was cancelled
        mark_dirty();
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
                "[I] Import  [Z] ZIP  [N] New  [Del] Delete  [/] Search  [G] Tags  [B] Fav  "
                "[F] Fav Images  [Shift+F] Fav Galleries  [Enter] Open  [Space] Select  "
                "[X] Export  [M] Move/Copy  [`] Switch  [Esc] Back  [L] List/Grid",
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

    if (naming_.active) {
        const float mw = W * 0.6f;
        const float mh = 120;
        const float mx = (W - mw) / 2;
        const float my = (H - mh) / 2;
        r.draw_round_rect({mx, my, mw, mh}, RADIUS, SURFACE);
        r.draw_round_rect({mx, my, mw, mh}, RADIUS, ACCENT, /*filled*/ false);
        r.draw_text(font_, mx + 16, my + 16, "New gallery name:", TEXT);
        draw_text_field(r, font_, {mx + 16, my + 56, mw - 32, 44}, naming_.buf, true);
    }

    // Delete-confirmation modal: names the target tile; deletion is irreversible.
    if (naming_.confirm_delete) {
        r.draw_rect({0, 0, W, H}, gfx::Color{8, 9, 12, 255});   // veil

        const float pw = 560;
        const float ph = 200;
        const float px = (W - pw) / 2;
        const float py = (H - ph) / 2;
        r.draw_round_rect({px, py, pw, ph}, RADIUS, SURFACE);
        r.draw_round_rect({px, py, pw, ph}, RADIUS, DANGER, /*filled*/ false);

        auto centered = [&](const std::string& s, float y, gfx::Color c) {
            const auto tw = static_cast<float>(font_.measure(s));
            r.draw_text(font_, px + (pw - tw) / 2, y, s, c);
        };

        const int s = nav_.selected();
        const vault::IndexNode* node =
            (s >= 0 && s < static_cast<int>(children_.size())) ? children_[s] : nullptr;
        const std::string name = node ? node->name : std::string{};
        centered("Delete \"" + fit_name(name, pw - 80) + "\"?", py + 28, TEXT);
        if (node && node->is_gallery()) {
            SubtreeCounts c;
            count_subtree(*node, c);
            centered("This permanently removes the gallery and everything in it.", py + 72, DANGER);
            centered("Contains " + describe_subtree(c) + ".", py + 104, DANGER);
        } else {
            centered("This permanently removes it from the vault.", py + 84, DANGER);
        }
        centered("[Esc/N] Cancel        [Y] Delete", py + ph - 50, TEXT_DIM);
    }

    search_.render(r, font_, W, H);
    tag_editor_.render(r, font_, W, H);
    transfer_.render(r, font_, W, H);
    quick_switch_.render(r, font_, W, H);

    // Zip conflict modal: drawn when awaiting a choice between FlattenMixed and SkipMixed.
    // Only shown after the naming dialog closes (so !naming_.active).
    if (naming_.zip.active && !naming_.active) {
        // Veil the whole window.
        r.draw_rect({0, 0, W, H}, gfx::Color{8, 9, 12, 255});

        const float pw = 600;
        const float ph = 200;
        const float px = (W - pw) / 2;
        const float py = (H - ph) / 2;
        r.draw_round_rect({px, py, pw, ph}, RADIUS, SURFACE);
        r.draw_round_rect({px, py, pw, ph}, RADIUS, ACCENT, /*filled*/ false);

        // Centre text helper.
        auto centered = [&](const std::string& s, float y, gfx::Color c) {
            const auto tw = static_cast<float>(font_.measure(s));
            r.draw_text(font_, px + (pw - tw) / 2, y, s, c);
        };

        centered("Mixed folders detected in archive.", py + 28, TEXT);
        centered("Cannot mix nested folders with flat gallery structure.", py + 60, TEXT_DIM);
        centered("[F] Flatten all files  |  [S] Skip those directories", py + ph - 50, TEXT_DIM);
    }
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

        // Favorite badge: a small gold square in the top-right corner.
        if (n->favorite) {
            const SDL_FRect badge{cellr.x + CELL - 8 - 18, cellr.y + 8, 18, 18};
            r.draw_round_rect(badge, RADIUS_SMALL, FAVORITE);
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
        // Favorite marker: a gold bar down the row's right edge.
        if (n->favorite)
            r.draw_rect({row.x + row.w - 4, row.y, 4, row.h}, FAVORITE);

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
        } else if (n->is_video()) {
            const auto& vm = n->vmeta;
            // DIMENSIONS column doubles as the video's length (its play duration).
            r.draw_text(font_, dims_x, ty, format_duration(vm.duration_us), meta_c);
            r.draw_text(font_, size_x, ty, format_size(vm.orig_size), meta_c);
            r.draw_text(font_, type_x, ty, video_codec_name(vm.codec), meta_c);
            r.draw_text(font_, date_x, ty, format_date(vm.created_ts), meta_c);
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
    // Video: a centred play-triangle badge over the poster (with a 1px dark
    // drop-shadow for contrast — the renderer's default blend mode is opaque).
    if (n.is_video()) {
        const float cx = box.x + box.w * 0.5f;
        const float cy = box.y + box.h * 0.5f;
        const float s  = std::min(box.w, box.h) * 0.16f;
        const SDL_FPoint a{cx - s * 0.5f, cy - s};
        const SDL_FPoint b{cx - s * 0.5f, cy + s};
        const SDL_FPoint c{cx + s, cy};
        r.draw_triangle({a.x + 2, a.y + 2}, {b.x + 2, b.y + 2}, {c.x + 2, c.y + 2},
                        gfx::Color{0, 0, 0, 255});
        r.draw_triangle(a, b, c, gfx::Color{255, 255, 255, 255});
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
