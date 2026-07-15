#include "ui/gallery_grid.h"

#include <algorithm>
#include <cctype>
#include <cstring>
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
#include "ui/gallery_sort.h"
#include "ui/grid_layout.h"
#include "ui/input.h"
#include "ui/progress_modal.h"
#include "ui/tag_list_parse.h"
#include "ui/tile_thumb.h"
#include "ui/waste_threshold.h"
#include "ui/widgets.h"
#include "ui/zip_import.h"
#include "vault/file_util.h"
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

// Apply a parsed tag list onto the node at `target` (slash-separated path),
// merging via Vault::add_tag (case-insensitive de-dupe handled by the vault).
// Returns {added, skipped} by comparing the node's tag count before/after, so
// no UI-side case-folding is needed. Kept free (not a GalleryGrid member) to
// keep the class under the S1448 method cap (Phase 21).
struct TagImportCounts { int added = 0; int skipped = 0; };
TagImportCounts apply_tag_list(vault::Vault& v, const std::string& target,
                               const std::vector<std::string>& tags)
{
    const auto slash = target.find_last_of('/');
    const std::string parent = slash == std::string::npos ? std::string{} : target.substr(0, slash);
    const std::string name   = slash == std::string::npos ? target : target.substr(slash + 1);

    auto tag_count = [&]() -> size_t {
        for (const auto* c : v.list(parent))
            if (c->name == name) return c->tags.size();
        return 0;
    };

    const size_t before = tag_count();
    for (const auto& t : tags) (void)v.add_tag(target, t);
    const size_t after = tag_count();

    const auto added = static_cast<int>(after - before);
    return {added, static_cast<int>(tags.size()) - added};
}

// Read, parse, and apply a picked tag-list file onto `target`. Returns a status
// summary (or sets `error` and returns ""). Free (not a member) so update()'s
// pump stays a flat call site under the S134 nesting limit (Phase 21).
std::string apply_tag_list_file(vault::Vault& v, const std::string& target,
                                const std::vector<std::string>& picked, std::string& error)
{
    if (target.empty() || picked.empty()) return {};
    auto bytes = platform::read_file(picked.front());
    if (!bytes) { error = "Could not read tag list."; return {}; }
    const auto counts = apply_tag_list(v, target, ui::parse_tag_list(*bytes));
    return std::format("Tag import: {} added, {} skipped", counts.added, counts.skipped);
}

// Centre the `cols` columns horizontally in a `win_w`-wide window so the left and
// right margins match (never tighter than OX).
GridSpec grid_spec(float win_w, int cols) noexcept
{
    const float used = static_cast<float>(cols) * CELL +
                       static_cast<float>(cols > 0 ? cols - 1 : 0) * GAP;
    const float ox = std::max(OX, (win_w - used) * 0.5f);
    return {cols, CELL, GAP, ox, OY};
}

// Leaf-invariant checks over the current listing (free, not members, to keep the
// class under the S1448 method cap). A gallery that holds sub-galleries can't take
// images; one that holds media can't take sub-galleries.
using ChildList = std::vector<const vault::IndexNode*>;
bool leaf_allows_images(const ChildList& children)
{
    return std::ranges::none_of(children,
                                [](const vault::IndexNode* c) { return c->is_gallery(); });
}
bool leaf_allows_galleries(const ChildList& children)
{
    return std::ranges::none_of(children,
                                [](const vault::IndexNode* c) { return c->is_media(); });
}

// Background-import progress modal (free, not a member, for the S1448 cap). Veils
// the grid — drawn instead of the tiles so no thumbnail is decrypted on the UI
// thread while the worker owns the vault. `cbz` only picks the wording.
void draw_import_progress(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H,
                          bool cbz, const ZipImportJob& job)
{
    const int total = job.total();
    const int done  = job.done();
    // "done / total" (total is 0 for the brief window before the first page).
    const char* unit = cbz ? "pages" : "files";
    const std::string count =
        total > 0 ? std::format("{} / {} {}", done, total, unit) : "Reading archive…";
    draw_op_progress(r, font, W, H,
                     {.title = cbz ? "Importing comic…" : "Importing archive…",
                      .count_line = count, .done = done, .total = total});
}

// Background file-op (export/delete/move/copy/compact) progress modal — same veil + bar as
// the import one, wording chosen by the running job's kind (Phase 25–26).
void draw_file_op_progress(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H,
                           const FileOpJob& job)
{
    std::string_view title = "Working…";
    std::string_view unit  = "items";
    using enum FileOpKind;
    switch (job.kind()) {
        case Export:   title = "Exporting…"; unit = "images"; break;
        case Delete:   title = "Deleting…";  unit = "items";  break;
        case Transfer: title = "Moving…";    unit = "files";  break;
        case Compact:  title = "Compacting…"; unit = "chunks"; break;
        case Import:   title = "Importing…"; unit = "files";  break;
        case None:     break;
    }
    const int total = job.total();
    const int done  = job.done();
    const std::string count =
        total > 0 ? std::format("{} / {} {}", done, total, unit) : "Preparing…";
    draw_op_progress(r, font, W, H,
                     {.title = title, .count_line = count, .done = done, .total = total});
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
    if (picked.empty()) return;

    // Run the decrypt→write on a worker thread (a big selection is slow) so the UI
    // stays responsive with a progress bar + cancel (Phase 25). The picked nodes
    // point into the live index, which the read-only export does not mutate and the
    // UI does not refresh while the job runs — so they stay valid on the worker.
    error_.clear();
    status_.clear();
    naming_.file_op.start_export(vault_, std::move(picked), dest, dest.string());
    mark_dirty();
}

void GalleryGrid::start_import()
{
    if (dialogs_.file.busy()) return;
    if (!leaf_allows_images(children_)) {
        error_ = "Can't import here: this gallery holds sub-galleries.";
        return;
    }
    error_.clear();
    dialogs_.file.open_images(win_.sdl_window());
}

void GalleryGrid::start_naming()
{
    if (!leaf_allows_galleries(children_)) {
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
        naming_.zip.cbz = false;
        naming_.zip.archive_backend = false;
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

void GalleryGrid::start_tag_editor(bool import_list)
{
    const int s = nav_.selected();
    if (s < 0 || s >= static_cast<int>(children_.size())) return;

    const vault::IndexNode* n = children_[s];
    const std::string base = nav_.path();
    const std::string full_path = base.empty() ? n->name : base + "/" + n->name;

    if (!import_list) { tag_editor_.open(full_path); return; }

    // Shift+G: import a tag list (.txt, one per line) onto the focused gallery.
    // The result is drained by update()'s TagList poller.
    if (dialogs_.file.busy() || transfer_.active()) return;
    if (!n->is_gallery()) { error_ = "Select a gallery to import a tag list onto."; return; }
    naming_.tag_target = full_path;
    error_.clear();
    dialogs_.file.open_tag_list(win_.sdl_window());
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
    // best-effort: favorite toggle failure is benign, UI re-reads state
    (void)vault_.toggle_favorite(full_path);
}

void GalleryGrid::cycle_gallery_sort()
{
    const std::string path = nav_.path();
    const auto next = ui::next_sort_key(vault::gallery_sort_key(vault_, path));
    // Reordering changes children_ (unlike a favorite-flag flip), so refresh
    // the listing on success; a failed persist (e.g. a race with lock) just
    // leaves the displayed order as-is.
    if (vault::set_gallery_sort(vault_, path, next) == vault::VaultResult::Ok) refresh();
}

SDL_Texture* GalleryGrid::thumb_texture(const vault::IndexNode& node)
{
    return tile_thumb_texture({vault_, cache_, thumbs_.worker, thumbs_.failed}, node);
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

void GalleryGrid::handle_password_key(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_TEXT_INPUT) { naming_.password.buf.push_utf8(e.text.text); return; }
    if (e.type != SDL_EVENT_KEY_DOWN) return;
    if (e.key.key == SDLK_BACKSPACE) {
        naming_.password.buf.backspace();
    } else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
        naming_.password.active = false;
        SDL_StopTextInput(win_.sdl_window());
        do_zip_import(naming_.zip.path, ui::ZipConflictPolicy::AskUser);
    } else if (e.key.key == SDLK_ESCAPE) {
        naming_.password.active = false;
        naming_.password.buf.clear();
        naming_.zip.active         = false;   // cancel the whole import
        naming_.zip.cbz            = false;
        naming_.zip.archive_backend = false;
        naming_.zip.needs_password  = false;
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

// Extract Shift+C compact handler to reduce handle_key_down's cognitive complexity (S3776).
void handle_shift_c_key(GalleryGrid& g, const SDL_KeyboardEvent& key)
{
    if (!((key.key == SDLK_C) && (key.mod & SDL_KMOD_SHIFT))) return;

    const uint64_t file_sz = vault::vault_file_bytes(g.vault_);
    const uint64_t waste_sz = g.vault_.wasted_bytes();
    // Only show the compact option if there's significant waste to reclaim.
    if (should_display_waste(waste_sz, file_sz)) {
        g.naming_.confirm_compact = true;
        g.error_.clear();
    } else {
        g.error_ = "No significant waste to reclaim.";
    }
}

// Extract Delete handler to reduce handle_key_down's cognitive complexity (S3776).
void handle_delete_key(GalleryGrid& g)
{
    const int s = g.nav_.selected();
    g.naming_.confirm_delete = s >= 0 && s < static_cast<int>(g.children_.size());
    g.error_ = g.naming_.confirm_delete ? std::string{} : "Nothing selected to delete.";
}

void GalleryGrid::handle_key_down(const SDL_KeyboardEvent& key)
{
    using enum GalleryView;
    // Zip-import + delete keep their guards/early-outs as plain ifs.
    if (key.key == SDLK_Z) {   // import zip archive (inlined to keep GalleryGrid <= 35 methods)
        if (dialogs_.file.busy() || transfer_.active()) return;
        error_.clear();
        dialogs_.file.open_zip(win_.sdl_window());
        return;
    }
    if (key.key == SDLK_DELETE) {   // confirm-delete the focused image/video/gallery
        handle_delete_key(*this);
        return;
    }
    if ((key.key == SDLK_C) && (key.mod & SDL_KMOD_SHIFT)) {   // Shift+C: confirm-compact the vault (Phase 26)
        handle_shift_c_key(*this, key);
        return;
    }
    // Single-action shortcut keys, grouped into one switch so this function's
    // cognitive complexity stays under the cpp:S3776 limit. Shift variants:
    // Shift+G imports a tag list, Shift+F opens favorite galleries, Shift+T the
    // tag overview. Plain T has no shortcut and falls through to navigation.
    if (is_quick_switch_key(key)) { quick_switch_.open(); return; }   // switch vault (`)
    switch (key.key) {
        case SDLK_L:     view_ = (view_ == Grid) ? List : Grid;             return;  // grid/list view
        case SDLK_X:     start_export();                                    return;  // export selection
        case SDLK_M:     start_transfer();                                  return;  // move to another vault
        case SDLK_SPACE: toggle_or_open();                                  return;
        case SDLK_G:     start_tag_editor((key.mod & SDL_KMOD_SHIFT) != 0); return;  // G edit / Shift+G import
        case SDLK_B:     toggle_favorite_current();                         return;  // favorite
        case SDLK_F:     request((key.mod & SDL_KMOD_SHIFT) ? NavKind::ToFavoriteGalleries
                                                            : NavKind::ToFavoriteImages); return;
        case SDLK_T:     if (key.mod & SDL_KMOD_SHIFT) { request(NavKind::ToTagOverview); return; }
                         break;
        case SDLK_S:     if (key.mod & SDL_KMOD_SHIFT) { cycle_gallery_sort(); return; }
                         break;
        case SDLK_U:     request(NavKind::ToggleKeepUnlocked);                      return;  // Phase 33
        default:         break;
    }
    // `/` is a shifted key on many non-US layouts, so the base keycode (key.key)
    // is the unmodified symbol (e.g. '7') and never equals SDLK_SLASH. The
    // is_*_key helpers (ui/keybindings.h) resolve the character the layout + held
    // modifiers actually produce: `/` opens the search overlay, Shift+/ ('?') the
    // advanced-search screen (Phase 18).
    if (is_search_key(key))          { search_.open();                     return; }
    if (is_advanced_search_key(key)) { request(NavKind::ToAdvancedSearch); return; }

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

bool GalleryGrid::handle_delete_confirm_key(const SDL_Event& e)
{
    if (!naming_.confirm_delete) return false;
    if (e.type != SDL_EVENT_KEY_DOWN) return true;   // modal swallows non-key events

    const SDL_Keycode k = e.key.key;
    if (k == SDLK_ESCAPE || k == SDLK_N) {
        naming_.confirm_delete = false;
        mark_dirty();
        return true;
    }
    if (k != SDLK_Y) return true;                    // swallow every other key

    // Run the removal on a worker thread with a progress modal + cancel (Phase 25).
    // A gallery subtree removal counts its descendants for the bar; a single media
    // is one item. take_outcome() (in poll_file_job) refreshes the listing.
    if (const int s = nav_.selected();
        s >= 0 && s < static_cast<int>(children_.size())) {
        const vault::IndexNode& n = *children_[s];
        int item_total = 1;
        if (n.is_gallery()) {
            SubtreeCounts c;
            count_subtree(n, c);
            item_total = c.images + c.videos + c.galleries + 1;   // +1 for the gallery itself
        }
        naming_.file_op.start_delete(vault_, nav_.path(), n.name, n.is_gallery(), item_total);
    }
    naming_.confirm_delete = false;
    mark_dirty();
    return true;
}

bool GalleryGrid::handle_compact_confirm_key(const SDL_Event& e)
{
    if (!naming_.confirm_compact) return false;
    if (e.type != SDL_EVENT_KEY_DOWN) return true;   // modal swallows non-key events

    const SDL_Keycode k = e.key.key;
    if (k == SDLK_ESCAPE || k == SDLK_N) {
        naming_.confirm_compact = false;
        mark_dirty();
        return true;
    }
    if (k != SDLK_Y) return true;                    // swallow every other key

    // Run compaction on a worker thread with a progress modal + cancel (Phase 26).
    // take_outcome() (in poll_file_job) shows the result.
    naming_.file_op.start_compact(vault_);
    naming_.confirm_compact = false;
    mark_dirty();
    return true;
}

bool GalleryGrid::handle_zip_conflict_key(const SDL_Event& e)
{
    if (!naming_.zip.active || naming_.active || naming_.password.active
        || e.type != SDL_EVENT_KEY_DOWN) return false;
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
            naming_.password.active = false;
            naming_.password.buf.clear();
            mark_dirty();
            return true;
        default:
            return false;
    }
}

// Overlays take input in priority order: search > tag_editor > transfer >
// quick_switch > export consent. Extracted from handle_event (cpp:S3776 —
// each of these blocks was a nested branch there; here they're flat top-level
// ifs, so the same logic costs far less cognitive complexity).
bool GalleryGrid::handle_overlay_event(const SDL_Event& e)
{
    if (search_.active()) {
        if (search_.handle_event(e)) {
            Nav nav = search_.take_nav();
            if (nav.kind != NavKind::None) request(nav.kind, std::move(nav.path), nav.index);
        }
        return true;
    }

    if (tag_editor_.active()) {
        (void)tag_editor_.handle_event(e);
        return true;
    }

    if (transfer_.active()) { (void)transfer_.handle_event(e); return true; }

    if (quick_switch_.active()) {
        (void)quick_switch_.handle_event(e);
        if (std::string p; quick_switch_.consume_choice(p))
            request(NavKind::ToUnlock, std::move(p));   // locks current, unlocks chosen
        return true;
    }

    // The export consent modal owns all input while it is up.
    if (consent_.active()) {
        if (e.type == SDL_EVENT_KEY_DOWN &&
            consent_.handle_key(e.key.key) == ConsentDialog::Result::Confirmed)
            dialogs_.folder.open(win_.sdl_window());
        return true;
    }

    return false;
}

void GalleryGrid::handle_event(const SDL_Event& e)
{
    // A background import / export / delete owns the vault; its Esc→cancel gate
    // swallows input until the worker finishes (a running transfer is owned by the
    // dialog, which does its own Esc→cancel below).
    if (handle_job_input(*this, e)) return;

    if (handle_overlay_event(e)) return;

    // The delete-confirmation modal owns all input while it is up.
    if (handle_delete_confirm_key(e)) return;

    // The compact-confirmation modal owns all input while it is up (Phase 26).
    if (handle_compact_confirm_key(e)) return;

    // The password-prompt modal owns all input while it is up (Phase 35).
    if (naming_.password.active) { handle_password_key(e); return; }

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
        case SDL_EVENT_MOUSE_WHEEL: {
            // Scroll without moving selection.
            const float scroll_step = (view_ == GalleryView::List)
                ? ROW_H * 2
                : (CELL + GAP) * 0.5f;
            scroll_ -= e.wheel.y * scroll_step;
            mark_dirty();
            break;
        }
        default: break;
    }
}

void GalleryGrid::pump_import()
{
    if (auto res = dialogs_.file.take_result(platform::FileDialog::Purpose::Images)) {
        if (!res->empty()) {
            std::vector<std::filesystem::path> paths;
            for (const auto& s : *res) paths.emplace_back(s);
            naming_.file_op.start_import(vault_, nav_.path(), std::move(paths));
        }
        mark_dirty();   // dialog closed (import launched or cancelled) — repaint
    }
}

namespace {
// Classify a picked archive's extension (Phase 34): whether it's a one-leaf
// comic-style import (cbz==true) and whether it routes through the libarchive
// backend (archive_backend==true) instead of miniz. Orthogonal axes: .cbz is
// {cbz, miniz}, .cbr/.cb7/.cbt are {cbz, archive}, .zip is {mirror, miniz},
// .7z/.rar/.tar(+.gz/.xz) are {mirror, archive}.
struct ArchiveExtKind { bool cbz; bool archive_backend; };
ArchiveExtKind classify_archive_ext(std::string_view ext)
{
    if (ext == ".cbz") return {true, false};
    if (ext == ".cbr" || ext == ".cb7" || ext == ".cbt") return {true, true};
    if (ext == ".7z" || ext == ".rar" || ext == ".tar" || ext == ".gz" || ext == ".xz")
        return {false, true};
    return {false, false};   // .zip and anything else: the existing miniz mirror/append path
}

// Copy a SecureTextField's typed bytes into an owned SecureBytes (moved into
// the background job's worker lambda). Empty when the field is empty, or if
// the allocation itself fails — the job/import layer already treats an empty
// password as "not yet supplied" (Phase 35), so a rare alloc failure here
// just degrades to that same, safe "ask again" behavior instead of copying
// into a too-small buffer (code-review fix: the resize() result must be
// checked before memcpy, or a failed resize leaves `pw` at size 0 while the
// memcpy below still writes f.length() bytes into it — a buffer overflow).
crypto::SecureBytes password_bytes(const SecureTextField& f)
{
    crypto::SecureBytes pw;
    if (!f.empty() && pw.resize(f.length()))
        std::memcpy(pw.data(), f.bytes().data(), f.length());
    return pw;
}
} // namespace

void GalleryGrid::pump_zip_import()
{
    auto res = dialogs_.file.take_result(platform::FileDialog::Purpose::Zip);
    if (!res) return;
    if (res->empty()) { mark_dirty(); return; }   // dialog cancelled — repaint

    const std::filesystem::path zp(res->front());
    std::string ext = zp.extension().string();   // ".cbz" / ".zip" / ".7z" / ...
    std::ranges::transform(ext, ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto [cbz, archive_backend] = classify_archive_ext(ext);

    // Phase 35: a password-protected zip/cbz (ZipCrypto "traditional"
    // encryption) always routes through the libarchive backend instead of
    // miniz — miniz has no decrypt path for any encryption flavor — and
    // needs a password prompt before it can import. WinZip-AES zips are
    // NOT detected here (m_is_encrypted covers both, but only traditional
    // encryption can actually be decrypted downstream — an AES zip will
    // fail the verification probe in import_archive with a generic error,
    // same as today, never an unsatisfiable retry loop).
    const bool needs_password = (ext == ".zip" || ext == ".cbz") && ui::zip_is_encrypted(zp);
    if (needs_password) archive_backend = true;

    if (cbz) {
        // CBZ/CBR/CB7/CBT: always a dedicated leaf gallery named after the archive.
        // start_naming() guards the leaf-invariant and (on success) prefills the
        // stem; finish_naming() then routes to the fixed one-leaf plan.
        start_naming();
        if (naming_.active) {
            // Prefill from the archive's meta.json title when present (Phase 27,
            // miniz .cbz only — the libarchive backend has no meta.json support
            // yet), else the filename stem (e.g. "MyComic" from "MyComic.cbz").
            // The text the user confirms is authoritative — the import never
            // overrides it.
            naming_.buf = archive_backend
                ? zp.stem().string()
                : ui::meta_gallery_name(ui::peek_archive_meta(zp), zp.stem().string());
            naming_.zip.path = zp;
            naming_.zip.dest = ui::ZipDest::NewGallery;
            naming_.zip.cbz = true;
            naming_.zip.archive_backend = archive_backend;
            naming_.zip.needs_password  = needs_password;
            naming_.zip.active = true;
        }
    } else if (leaf_allows_images(children_) && !leaf_allows_galleries(children_)) {
        // Current gallery holds only media (leaf): import with Append (no name prompt).
        naming_.zip.path = zp;
        naming_.zip.gallery_name.clear();
        naming_.zip.dest = ui::ZipDest::Append;
        naming_.zip.cbz = false;
        naming_.zip.archive_backend = archive_backend;
        naming_.zip.needs_password  = needs_password;
        naming_.zip.active = true;
        do_zip_import(zp, ui::ZipConflictPolicy::AskUser);
    } else {
        // Current is empty or holds sub-galleries: prompt for new gallery name.
        start_naming();   // reuse the naming flow
        // Prefill from meta.json title → filename stem, as in the CBZ branch.
        naming_.buf = archive_backend
            ? zp.stem().string()
            : ui::meta_gallery_name(ui::peek_archive_meta(zp), zp.stem().string());
        naming_.zip.path = zp;
        naming_.zip.dest = ui::ZipDest::NewGallery;
        naming_.zip.cbz = false;
        naming_.zip.archive_backend = archive_backend;
        naming_.zip.needs_password  = needs_password;
        naming_.zip.active = true;
    }
    mark_dirty();   // dialog closed (picked) — repaint
}

void GalleryGrid::do_zip_import(const std::filesystem::path& zip_path, ui::ZipConflictPolicy policy)
{
    // The gallery name and destination come from naming_.zip.
    const std::string gallery_name = naming_.zip.gallery_name;
    const std::string base_gallery = nav_.path();
    const ui::ZipDest dest = naming_.zip.dest;

    // Both CBZ and ZIP imports run on a background thread so the UI never freezes
    // on a large archive (Phase 24 fix) — synchronously here would freeze it on the
    // name popup ("locked in"). update() drains the result; while the worker owns
    // the vault the UI shows only the progress modal. CBZ is a fixed one-leaf plan;
    // a ZIP with mixed folders comes back needs_resolution (nothing written) so
    // update() shows the Flatten/Skip modal, and F/S re-enters here with the chosen
    // policy. naming_.zip stays populated across that round-trip and is cleared on a
    // terminal outcome. archive_backend (Phase 34) picks the libarchive-backed job
    // methods for .7z/.rar/.tar(+variants)/.cbr/.cb7/.cbt — same threading/progress
    // contract, only the decompression backend differs. Phase 35: needs_password
    // additionally threads naming_.password.buf through on every (re)launch —
    // empty on the very first attempt (before the user has typed anything), which
    // correctly comes back needs_password=true and opens the prompt.
    if (naming_.zip.cbz) {
        if (naming_.zip.archive_backend)
            naming_.import_job.start_archive_cbz(vault_, zip_path, base_gallery, gallery_name,
                                                 naming_.zip.needs_password,
                                                 password_bytes(naming_.password.buf));
        else
            naming_.import_job.start_cbz(vault_, zip_path, base_gallery, gallery_name);
    } else {
        if (naming_.zip.archive_backend)
            naming_.import_job.start_archive(vault_, zip_path, ui::ZipImportTarget{dest, policy},
                                             base_gallery, gallery_name, naming_.zip.needs_password,
                                             password_bytes(naming_.password.buf));
        else
            naming_.import_job.start_zip(vault_, zip_path, dest, base_gallery, gallery_name, policy);
    }
    mark_dirty();
}

// Extract cancelled-import waste-hint logic to reduce poll_import_job's nesting (S134).
void set_cancelled_import_status(GalleryGrid& g, int imported, const char* noun)
{
    // User pressed Esc during import — check if waste hints are needed (Phase 26).
    const uint64_t waste = g.vault_.wasted_bytes();
    if (should_hint_cancelled_import_waste(waste)) {
        g.status_ = std::format("Import cancelled — {} reclaimable, press [Shift+C]",
                               format_size(waste));
    } else {
        g.status_ = std::format("Import cancelled — {} {} imported", imported, noun);
    }
}

void poll_import_job(GalleryGrid& g)
{
    // Only touch the vault once the worker has fully finished (take_outcome joins
    // the thread), so the single-thread file-handle invariant holds.
    if (auto oc = g.naming_.import_job.take_outcome()) {
        const bool cbz = g.naming_.zip.cbz;   // "page" (comic) vs "file" (zip) wording
        const char* fail = cbz ? "CBZ import failed." : "ZIP import failed.";
        if (!oc->ok) {
            g.error_ = oc->error.empty() ? fail : oc->error;
            g.naming_.zip.active          = false;
            g.naming_.zip.cbz             = false;
            g.naming_.zip.archive_backend = false;
            g.naming_.zip.needs_password  = false;
            g.naming_.password.active     = false;
            g.naming_.password.buf.clear();
        } else if (oc->needs_resolution) {
            // ZIP with mixed folders: keep naming_.zip active so the Flatten/Skip
            // modal shows; the worker wrote nothing. F/S re-launches with a policy.
        } else if (oc->needs_password) {
            // Encrypted zip/cbz: keep naming_.zip active (mirrors needs_resolution)
            // and open the password modal. `retry` distinguishes "nothing typed
            // yet" from "that password was wrong" for the modal's message; the
            // just-tried (wrong) password is cleared so the field starts empty for
            // the next attempt (Phase 35).
            g.naming_.password.retry = !g.naming_.password.buf.empty();
            g.naming_.password.buf.clear();
            g.naming_.password.active = true;
            SDL_StartTextInput(g.win_.sdl_window());
        } else {
            const char* noun = cbz ? "page" : "file";
            if (oc->cancelled) {
                set_cancelled_import_status(g, oc->imported, noun);
            } else {
                g.status_ = std::format("Imported {} {}{}, skipped {}", oc->imported, noun,
                                        oc->imported == 1 ? "" : "s", oc->skipped);
            }
            g.naming_.zip.active          = false;
            g.naming_.zip.cbz             = false;
            g.naming_.zip.archive_backend = false;
            g.naming_.zip.needs_password  = false;
            g.naming_.password.active     = false;
            g.naming_.password.buf.clear();
            g.refresh();   // the import mutated the index tree — rebuild children_
        }
    }
    g.mark_dirty();
}

bool vault_busy(const GalleryGrid& g)
{
    return g.naming_.import_job.active() || g.naming_.file_op.active() || g.transfer_.job_active();
}

void poll_file_job(GalleryGrid& g)
{
    // take_outcome() joins the worker before returning, so touching the vault
    // (refresh) afterwards respects the single-thread file-handle invariant.
    if (auto oc = g.naming_.file_op.take_outcome()) {
        if (!oc->ok && !oc->error.empty()) g.error_ = oc->error;
        else                               g.status_ = oc->status;
        g.sel_.clear();
        g.refresh();   // a delete changed the listing; harmless after an export
    }
    g.mark_dirty();
}

// While a background job owns the vault, swallow all input except Esc → cooperative
// cancel. Free friend to keep GalleryGrid under the cpp:S1448 method cap and to keep
// handle_event()'s cognitive complexity (S3776) down (Phase 25).
bool handle_job_input(GalleryGrid& g, const SDL_Event& e)
{
    const bool esc = e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE;
    if (g.naming_.import_job.active()) {
        if (esc) g.naming_.import_job.cancel();
        return true;
    }
    if (g.naming_.file_op.active()) {
        if (esc) g.naming_.file_op.cancel();
        return true;
    }
    return false;
}

// Bundle the 6 data parameters for draw_footer_status to reduce cognitive complexity (S107).
struct FooterStatus {
    bool show_waste;
    bool show_selection;
    uint64_t waste_sz;
    int selection_count;
    const std::string& error;
    const std::string& status;
};

// Build the breadcrumb line, appending the active sort indicator once it's
// non-Manual (Phase 37) — extracted (like draw_footer_status) to keep
// render()'s cognitive complexity under the cpp:S3776 limit.
std::string breadcrumb_text(const NavModel& nav, vault::SortKey sort_key)
{
    std::string crumb = "/";
    for (const auto& s : nav.segments()) { crumb += s; crumb += '/'; }
    if (const auto label = ui::sort_key_label(sort_key); !label.empty())
        crumb += "   Sort: " + label;
    return crumb;
}

// Extract footer + waste/selection rendering to reduce render's cognitive complexity (S3776).
void draw_footer_status(gfx::Renderer& r, gfx::FontAtlas& font, float x_offset, float bottom,
                        const FooterStatus& data)
{
    using namespace gfx::theme;
    if (data.show_waste || data.show_selection) {
        std::string footer;
        if (data.show_selection)
            footer = std::format("{} selected", data.selection_count);
        if (data.show_waste) {
            if (!footer.empty()) footer += " · ";
            footer += "Waste: " + format_size(data.waste_sz) + " [Shift+C]";
        }
        const gfx::Color color = data.show_selection ? ACCENT : TEXT_DIM;
        r.draw_text(font, x_offset, 120, footer, color);
    }

    if (!data.error.empty())
        r.draw_text(font, x_offset, bottom - 36, data.error, DANGER);
    else if (!data.status.empty())
        r.draw_text(font, x_offset, bottom - 36, data.status, OK);
}

void GalleryGrid::update(double)
{
    // A background import owns the vault's file handle on its worker thread, so the
    // UI must not read the vault (thumbnails/listing) until it finishes — poll only.
    if (naming_.import_job.active()) { poll_import_job(*this); return; }
    if (naming_.file_op.active())    { poll_file_job(*this);   return; }

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

        // Tag-list import (Shift+G, Phase 21). The .txt carries tag metadata only —
        // not key material or decrypted vault content — so reading it is invariant-safe.
        if (auto res = dialogs_.file.take_result(platform::FileDialog::Purpose::TagList)) {
            if (!res->empty()) {
                status_ = apply_tag_list_file(vault_, naming_.tag_target, *res, error_);
                refresh();
            }
            naming_.tag_target.clear();
            mark_dirty();
        }
    }

    if (auto dest = dialogs_.folder.take_result()) {
        if (!dest->empty()) do_export(*dest);   // empty => the picker was cancelled
        mark_dirty();
    }

    // Update scroll to keep the selected item visible.
    const int sel_idx = nav_.selected();
    const auto H = static_cast<float>(win_.height());
    if (sel_idx >= 0 && sel_idx < static_cast<int>(children_.size())) {
        if (view_ == GalleryView::List) {
            // For list view: item at row sel_idx
            const float item_top = OY + LIST_HEADER + static_cast<float>(sel_idx) * ROW_H;
            const float item_bottom = item_top + ROW_H;
            // Content height = header + (num_items * row_height)
            const float content_height = OY + LIST_HEADER + static_cast<float>(children_.size()) * ROW_H;
            // Apply selection-following scroll
            scroll_ = ui::ensure_visible(scroll_, item_top, item_bottom, OY + LIST_HEADER, H);
            scroll_ = ui::clamp_scroll(scroll_, content_height, H);
        } else {
            // For grid view: item at position computed from grid_cell_rect
            const auto W = static_cast<float>(win_.width());
            const SDL_FRect cellr = grid_cell_rect(sel_idx, grid_spec(W, cols_));
            const float item_top = cellr.y;
            const float item_bottom = cellr.y + CELL;
            // Content height = number of rows * (cell_height + gap) - gap + top offset
            const int cols = grid_columns(W - 2 * OX, CELL, GAP);
            const int total_rows = (static_cast<int>(children_.size()) + cols - 1) / cols;
            const float content_height = OY + static_cast<float>(total_rows) * (CELL + GAP);
            // Apply selection-following scroll
            scroll_ = ui::ensure_visible(scroll_, item_top, item_bottom, OY, H);
            scroll_ = ui::clamp_scroll(scroll_, content_height, H);
        }
    }
}

void GalleryGrid::render(gfx::Renderer& r)
{
    const auto W = static_cast<float>(win_.width());
    const auto H = static_cast<float>(win_.height());

    // While a background job runs, the worker thread owns the vault — drawing tiles
    // would decrypt thumbnails on this thread and race it. Show only the progress
    // modal over an empty backdrop. The transfer dialog draws its own progress modal.
    if (naming_.import_job.active()) {
        draw_import_progress(r, font_, W, H, naming_.zip.cbz, naming_.import_job);
        return;
    }
    if (naming_.file_op.active()) {
        draw_file_op_progress(r, font_, W, H, naming_.file_op);
        return;
    }
    if (transfer_.job_active()) {
        transfer_.render(r, font_, W, H);   // draws the veiling transfer progress modal
        return;
    }

    using namespace gfx::theme;
    const std::string crumb = breadcrumb_text(nav_, vault::gallery_sort_key(vault_, nav_.path()));
    r.draw_text(font_, OX, 40, fit_name(crumb, W - 2 * OX), TEXT_DIM);
    r.draw_text(font_, OX, 90,
                "[I] Import  [Z] ZIP/CBZ  [N] New  [Del] Delete  [/] Search  [?] Advanced  "
                "[G] Tags  [Shift+G] Tag list  [Shift+T] Tags Overview  "
                "[B] Fav  [F] Fav Images  [Shift+F] Fav Galleries  "
                "[Enter] Open  [Space] Select  [X] Export  [M] Move/Copy  [`] Switch  [Esc] Back  [L] List/Grid  "
                "[Shift+S] Sort  [U] Keep unlocked",
                TEXT_FAINT);

    // Show waste hint if it exceeds display threshold (Phase 26).
    // Combine with selection count on the same line to avoid collision.
    const uint64_t file_sz = vault::vault_file_bytes(vault_);
    const uint64_t waste_sz = vault_.wasted_bytes();
    const bool show_waste = should_display_waste(waste_sz, file_sz);
    const bool show_selection = !sel_.empty();

    draw_footer_status(r, font_, OX, H, FooterStatus{
        .show_waste = show_waste,
        .show_selection = show_selection,
        .waste_sz = waste_sz,
        .selection_count = static_cast<int>(sel_.count()),
        .error = error_,
        .status = status_
    });

    if (view_ == GalleryView::List) render_list(r, W, H);
    else                            render_grid(r, W, H);

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

    // Compact-confirmation modal: shows the waste to reclaim (Phase 26).
    if (naming_.confirm_compact) {
        r.draw_rect({0, 0, W, H}, gfx::Color{8, 9, 12, 255});   // veil

        const float pw = 560;
        const float ph = 200;
        const float px = (W - pw) / 2;
        const float py = (H - ph) / 2;
        r.draw_round_rect({px, py, pw, ph}, RADIUS, SURFACE);
        r.draw_round_rect({px, py, pw, ph}, RADIUS, ACCENT, /*filled*/ false);

        auto centered = [&](const std::string& s, float y, gfx::Color c) {
            const auto tw = static_cast<float>(font_.measure(s));
            r.draw_text(font_, px + (pw - tw) / 2, y, s, c);
        };

        const uint64_t compact_waste = vault_.wasted_bytes();
        const std::string waste_str = format_size(compact_waste);

        centered("Compact vault?", py + 28, TEXT);
        centered("Reclaim " + waste_str + " of wasted space.", py + 72, TEXT);
        centered("[Esc/N] Cancel        [Y] Compact", py + ph - 50, TEXT_DIM);
    }

    search_.render(r, font_, W, H);
    tag_editor_.render(r, font_, W, H);
    transfer_.render(r, font_, W, H);
    quick_switch_.render(r, font_, W, H);

    // Zip conflict modal: drawn when awaiting a choice between FlattenMixed and SkipMixed.
    // Only shown after the naming dialog closes (so !naming_.active).
    if (naming_.zip.active && !naming_.active && !naming_.password.active) {
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

    // Password-prompt modal: shown when the import outcome reported
    // needs_password (encrypted zip/cbz) — masked text entry, mirrors the
    // UnlockScreen password field's styling (Phase 35).
    if (naming_.password.active) {
        r.draw_rect({0, 0, W, H}, gfx::Color{8, 9, 12, 255});   // veil

        const float pw = 480;
        const float ph = 160;
        const float px = (W - pw) / 2;
        const float py = (H - ph) / 2;
        r.draw_round_rect({px, py, pw, ph}, RADIUS, SURFACE);
        r.draw_round_rect({px, py, pw, ph}, RADIUS, ACCENT, /*filled*/ false);

        auto centered = [&](const std::string& s, float y, gfx::Color c) {
            const auto tw = static_cast<float>(font_.measure(s));
            r.draw_text(font_, px + (pw - tw) / 2, y, s, c);
        };

        centered(naming_.password.retry ? "Incorrect passphrase." : "Archive password required.",
                 py + 20, naming_.password.retry ? DANGER : TEXT);
        draw_text_field(r, font_, {px + 20, py + 52, pw - 40, 44},
                        std::string(naming_.password.buf.length(), '*'), true);
        centered("[Enter] Unlock        [Esc] Cancel", py + ph - 26, TEXT_DIM);
    }
}

void GalleryGrid::render_grid(gfx::Renderer& r, float W, float H)
{
    using namespace gfx::theme;
    cols_ = grid_columns(W - 2 * OX, CELL, GAP);
    const auto [first_idx, last_idx] = grid_visible_range(
        scroll_, CELL, GAP, OY, H, cols_, static_cast<int>(children_.size()));
    // If the grid is empty, the range will be {0, -1}; the loop handles this correctly.
    for (int i = first_idx; i <= last_idx; ++i) {
        if (i < 0 || i >= static_cast<int>(children_.size())) continue;
        SDL_FRect cellr = grid_cell_rect(i, grid_spec(W, cols_));
        // Apply scroll offset to cell Y position.
        cellr.y -= scroll_;
        const vault::IndexNode* n = children_[i];
        const bool sel = (i == nav_.selected());
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
        if (sel_.contains(i)) {
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

// Context struct for list-row metadata rendering (bundled params, reduce S107).
struct ListRowMetaContext {
    gfx::Renderer& r;
    gfx::FontAtlas& font;
    float dims_x;
    float size_x;
    float type_x;
    float date_x;
    float ty;
};

// Extract per-row metadata drawing to reduce render_list's cognitive complexity (S3776).
void draw_list_row_metadata(const ListRowMetaContext& ctx, const vault::IndexNode* n, bool sel)
{
    using namespace gfx::theme;
    const gfx::Color meta_c = sel ? TEXT : TEXT_DIM;
    if (n->is_gallery()) {
        ctx.r.draw_text(ctx.font, ctx.dims_x, ctx.ty, "-", meta_c);
        ctx.r.draw_text(ctx.font, ctx.size_x, ctx.ty, "-", meta_c);
        ctx.r.draw_text(ctx.font, ctx.type_x, ctx.ty, "DIR", meta_c);
        ctx.r.draw_text(ctx.font, ctx.date_x, ctx.ty, "-", meta_c);
    } else if (n->is_video()) {
        const auto& vm = n->vmeta;
        ctx.r.draw_text(ctx.font, ctx.dims_x, ctx.ty, format_duration(vm.duration_us), meta_c);
        ctx.r.draw_text(ctx.font, ctx.size_x, ctx.ty, format_size(vm.orig_size), meta_c);
        ctx.r.draw_text(ctx.font, ctx.type_x, ctx.ty, video_codec_name(vm.codec), meta_c);
        ctx.r.draw_text(ctx.font, ctx.date_x, ctx.ty, format_date(vm.created_ts), meta_c);
    } else {
        const auto& m = n->meta;
        ctx.r.draw_text(ctx.font, ctx.dims_x, ctx.ty, format_dimensions(m.width, m.height), meta_c);
        ctx.r.draw_text(ctx.font, ctx.size_x, ctx.ty, format_size(m.orig_size), meta_c);
        ctx.r.draw_text(ctx.font, ctx.type_x, ctx.ty, image_format_name(m.format), meta_c);
        ctx.r.draw_text(ctx.font, ctx.date_x, ctx.ty, format_date(m.created_ts), meta_c);
    }
}

void GalleryGrid::render_list(gfx::Renderer& r, float W, float H)
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

    // Column header + separator (fixed, not scrolled).
    const float hy = font_.text_top_for_center(OY + (LIST_HEADER - 6) * 0.5f);
    r.draw_text(font_, OX, hy, "NAME", TEXT_FAINT);
    r.draw_text(font_, dims_x, hy, "DIMENSIONS", TEXT_FAINT);
    r.draw_text(font_, size_x, hy, "SIZE", TEXT_FAINT);
    r.draw_text(font_, type_x, hy, "TYPE", TEXT_FAINT);
    r.draw_text(font_, date_x, hy, "DATE", TEXT_FAINT);
    r.draw_rect({OX, OY + LIST_HEADER - 6, rw, 1.0f}, BORDER);

    const auto [first_idx, last_idx] = list_visible_range(
        scroll_, ROW_H, OY + LIST_HEADER, H, static_cast<int>(children_.size()));
    for (int i = first_idx; i <= last_idx; ++i) {
        if (i < 0 || i >= static_cast<int>(children_.size())) continue;
        const vault::IndexNode* n = children_[i];
        const bool sel = (i == nav_.selected());
        SDL_FRect row{OX, OY + LIST_HEADER + static_cast<float>(i) * ROW_H,
                      rw, ROW_H - 6};
        // Apply scroll offset to row Y position.
        row.y -= scroll_;
        if (sel) {
            r.draw_round_rect(row, RADIUS_SMALL, SURFACE_HI);
            r.draw_round_rect(row, RADIUS_SMALL, ACCENT, /*filled*/ false);
        }
        // Export-selection marker: an accent bar down the row's left edge.
        if (sel_.contains(i))
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

        // Draw metadata columns for this row (galleries/videos/images have different displays).
        const ListRowMetaContext meta_ctx{r, font_, dims_x, size_x, type_x, date_x, ty};
        draw_list_row_metadata(meta_ctx, n, sel);
    }
}

void GalleryGrid::draw_tile_thumb(gfx::Renderer& r, const vault::IndexNode& n,
                                  const SDL_FRect& box)
{
    ui::draw_tile_thumb(r, font_, {vault_, cache_, thumbs_.worker, thumbs_.failed}, n, box);
}

int GalleryGrid::hit_test(float mx, float my) const
{
    const auto count = static_cast<int>(children_.size());
    // Add scroll offset to mouse Y to convert from viewport to document coordinates.
    const float my_doc = my + scroll_;
    if (view_ == GalleryView::List) {
        const float top = OY + LIST_HEADER;
        if (mx < OX || mx > static_cast<float>(win_.width()) - OX || my_doc < top) return -1;
        const auto idx = static_cast<int>((my_doc - top) / ROW_H);
        return (idx >= 0 && idx < count) ? idx : -1;
    }
    return grid_hit(mx, my_doc, count, grid_spec(static_cast<float>(win_.width()), cols_));
}

std::string GalleryGrid::fit_name(std::string_view name, float max_w) const
{
    return elide_middle(name, static_cast<int>(max_w),
                        [this](std::string_view s) { return font_.measure(s); });
}

} // namespace ui
