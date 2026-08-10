#include "ui/tag_overview.h"

#include <algorithm>
#include <format>
#include <span>
#include <string>
#include <utility>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "platform/paths.h"
#include "ui/prompt_layout.h"
#include "ui/tag_chip.h"
#include "ui/tag_dict_import.h"
#include "ui/tag_json_parse.h"
#include "ui/tag_list_parse.h"
#include "ui/tag_overview_model.h"
#include "ui/text_input_event.h"
#include "ui/widgets.h"
#include "vault/index.h"
#include "vault/vault.h"
#include "vault/vault_search.h"

namespace ui {

namespace {
constexpr float OX  = 40;    // left margin
constexpr float OY  = 150;   // list top
constexpr float PAD = 9;     // vertical padding inside a row

// Prompt sizing: scales to window width with sensible bounds (used by import summary modal)
constexpr float PROMPT_PAD = 16.0f;           // internal padding in prompt box

// Phase 54 removed this file's private UTF-8 truncation and tail-clipping
// helpers: TextInputModel's byte cap does the former (on whole characters) and
// layout_text_field does the latter, for every field in the app rather than
// just this one.

// List geometry shared by render() + row_at() so a click lands on the row it
// looks like it lands on. `first` keeps the selection roughly centred when the
// list is taller than the viewport.
struct ListGeom {
    float row_h;
    int   visible;
    int   first;
};

ListGeom compute_geom(float ph, float win_h, int count, int sel)
{
    const float row_h      = 2 * ph + 3 * PAD;  // Two-line row: chip/counts + description
    const float bottom     = win_h - 24.0f;
    const float viewport_h = bottom - OY;
    const int   visible    = tag_overview_page_size(viewport_h, row_h);
    int         first      = 0;
    if (count > visible) first = std::clamp(sel - visible / 2, 0, count - visible);
    return {row_h, visible, first};
}

std::string count_label(int galleries, int images)
{
    return std::format("{} {}   {} {}",
                       galleries, galleries == 1 ? "gallery" : "galleries",
                       images, images == 1 ? "image" : "images");
}
// Helper: draw a single tag overview row
void draw_tag_row(gfx::Renderer& r, gfx::FontAtlas& font, float W, float y, float row_h,
                  float ph, const TagTally& tally, bool is_selected,
                  const std::vector<vault::TagCategory>& cats)
{
    using namespace gfx::theme;
    const float ty = y + (ph - 4) * 0.5f;
    const SDL_FRect row{OX, y, W - 2 * OX, row_h - 4};

    if (is_selected) r.draw_selection_glow(row, RADIUS, ACCENT);
    r.draw_round_rect(row, RADIUS, is_selected ? SURFACE_HI : SURFACE);
    r.draw_round_rect(row, RADIUS, is_selected ? ACCENT : BORDER, /*filled*/ false);

    // Line 1: tag chip and counts
    const std::string counts = count_label(tally.gallery_count, tally.image_count);
    const float cx = W - OX - 14 - static_cast<float>(font.measure(counts));
    draw_tag_chips(r, font, OX + 14, y + (ph - CHIP_ROW_H) * 0.5f,
                   cx - (OX + 14) - 12, std::span(&tally.tag, 1), cats);
    r.draw_text(font, cx, ty, counts, TEXT_DIM);

    // Line 2: description (or placeholder)
    const float desc_y = y + ph + PAD;
    const float max_desc_w = W - (OX + 14) - OX - 14;
    const std::string shown_desc = tally.description.empty()
        ? std::string("(no description — [E] to add)")
        : fit_text(font, tally.description, max_desc_w);
    r.draw_text(font, OX + 14, desc_y, shown_desc,
               tally.description.empty() ? TEXT_FAINT : TEXT_DIM);
}

} // namespace

TagOverviewScreen::TagOverviewScreen(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                                     platform::VaultRegistry& registry, std::string active_path,
                                     platform::FileDialog& file_dialog)
    : win_(win), font_(font), vault_(vault), file_dialog_(file_dialog),
      quick_switch_(registry, std::move(active_path))
{
}

void TagOverviewScreen::on_enter()
{
    SDL_StartTextInput(win_.sdl_window());
    reload();
}

void TagOverviewScreen::on_exit()
{
    SDL_StopTextInput(win_.sdl_window());
}

void TagOverviewScreen::reload()
{
    all_ = vault::VaultSearch(vault_).tag_overview();
    rebuild();
}

void TagOverviewScreen::rebuild()
{
    shown_ = filter_tags(all_, filter_.str());
    sort_tags(shown_, sort_);
    nav_.set_count(static_cast<int>(shown_.size()));
}

void TagOverviewScreen::open_selected()
{
    const int s = nav_.selected();
    if (s < 0 || s >= static_cast<int>(shown_.size())) return;
    request(NavKind::ToTagGalleries, shown_[s].tag, 0);
}

int TagOverviewScreen::row_at(float my) const
{
    const auto g = compute_geom(font_.pixel_height(), static_cast<float>(win_.height()),
                                static_cast<int>(shown_.size()), nav_.selected());
    if (my < OY) return -1;
    const int row = g.first + static_cast<int>((my - OY) / g.row_h);
    return (row >= g.first && row < g.first + g.visible && row < static_cast<int>(shown_.size()))
               ? row : -1;
}

void TagOverviewScreen::open_tag_dict_picker()
{
    if (file_dialog_.busy()) return;
    error_.clear();
    file_dialog_.open_tag_json(win_.sdl_window());
}

void TagOverviewScreen::import_tag_dict(const std::string& path)
{
    // A .json tag dictionary carries vocabulary only — no key material and no
    // decrypted vault content — so reading it is invariant-safe. The path came
    // back through platform::normalize_user_path in the dialog callback.
    const auto bytes = platform::read_file(path);
    if (!bytes) { error_ = "Could not read the tag dictionary."; return; }

    const TagDictParseResult parsed = parse_tag_dict_json(*bytes);
    if (parsed.entries.empty() && parsed.malformed_skipped == 0 &&
        parsed.over_cap_skipped == 0) {
        error_ = "That file holds no tag entries.";
        return;
    }

    auto       settings = vault::vault_settings(vault_);
    const auto summary  = apply_tag_dict(settings, parsed);
    // One commit for the whole file, not one per entry. It can fail, and a
    // failed commit must never be reported as a successful import.
    if (vault::set_vault_settings(vault_, std::move(settings)) != vault::VaultResult::Ok) {
        error_ = "Could not save the imported tag dictionary";
        return;
    }

    error_.clear();
    summary_title_  = "Tag dictionary imported";
    import_summary_ = tag_dict_summary_lines(summary);
    reload();
}

// The Ctrl+X confirm is modal like the compact confirm on the gallery grid:
// Y runs the cleanup, Esc/N cancels, every other key is swallowed.
bool TagOverviewScreen::handle_prune_confirm_key(const SDL_Event& e)
{
    if (!confirm_prune_) return false;
    if (e.type != SDL_EVENT_KEY_DOWN) return true;   // modal swallows non-key events

    const SDL_Keycode k = e.key.key;
    if (k == SDLK_ESCAPE || k == SDLK_N) {
        confirm_prune_ = false;
        return true;
    }
    if (k != SDLK_Y) return true;                    // swallow every other key

    confirm_prune_ = false;
    run_junk_tag_cleanup();
    return true;
}

void TagOverviewScreen::run_junk_tag_cleanup()
{
    // Junk = no renderable text (ui::tag_has_renderable_text): tags that draw
    // as empty bracket shells ("[()]", "[]", "()") in the ASCII-only UI font.
    vault::PruneTagsStats stats;
    if (vault_.prune_tags(tag_has_renderable_text, &stats) != vault::VaultResult::Ok) {
        error_ = "Could not save the cleaned tags.";
        return;
    }

    error_.clear();
    summary_title_ = "Junk-tag cleanup";
    if (stats.tags_removed == 0) {
        import_summary_ = {"No junk tags found."};
    } else {
        import_summary_ = {std::format("Removed {} junk tag{} from {} item{}.",
                                       stats.tags_removed, stats.tags_removed == 1 ? "" : "s",
                                       stats.nodes_touched, stats.nodes_touched == 1 ? "" : "s")};
        reload();
    }
}

void TagOverviewScreen::update(double dt)
{
    (void)dt;
    if (auto res = file_dialog_.take_result(platform::FileDialog::Purpose::TagJson)) {
        if (!res->empty()) import_tag_dict(res->front());   // empty => cancelled
        mark_dirty();
    }
}

void TagOverviewScreen::handle_key_down_in_browse_mode(const SDL_KeyboardEvent& key)
{
    if (is_quick_switch_key(key)) { quick_switch_.open(); return; }
    // A modifier chord, not a bare letter: '/' aside, plain keys belong to the
    // filter field and the E prompt.
    if (key.key == SDLK_I && (key.mod & SDL_KMOD_CTRL) != 0) {
        open_tag_dict_picker();
        return;
    }
    if (key.key == SDLK_X && (key.mod & SDL_KMOD_CTRL) != 0) {
        confirm_prune_ = true;
        error_.clear();
        return;
    }
    if (key.key == SDLK_T && (key.mod & SDL_KMOD_CTRL) != 0) {
        template_editor_.open();
        return;
    }
    switch (key.key) {
        case SDLK_UP:       nav_.move(-1);                              break;
        case SDLK_DOWN:     nav_.move(1);                               break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: open_selected();                           break;
        case SDLK_TAB:
            sort_ = (sort_ == TagSort::Name) ? TagSort::Count : TagSort::Name;
            rebuild();
            break;
        case SDLK_E:
            if (nav_.selected() >= 0 && nav_.selected() < static_cast<int>(shown_.size())) {
                const std::string& tag = shown_[nav_.selected()].tag;
                const auto& s = vault::vault_settings(vault_);  // const ref binds temporary lifetime
                std::string cat(vault::tag_category_prefix(tag));
                auto tmpl = vault::category_template(s, cat);
                fields_form_.open(tag, std::move(cat),
                                  std::vector<std::string>(tmpl.begin(), tmpl.end()),
                                  /*with_description=*/true);
                fields_form_.skip_next_text_input();  // The 'E' that opened the form arrives as a text event
                error_.clear();
                SDL_StartTextInput(win_.sdl_window());
            }
            break;
        case SDLK_SLASH:
            filtering_ = true;
            SDL_StartTextInput(win_.sdl_window());
            break;
        case SDLK_BACKSPACE:
        case SDLK_ESCAPE:
            request(NavKind::ToGallery, "", 0);
            break;
        default: break;
    }
}

void TagOverviewScreen::handle_event(const SDL_Event& e)
{
    if (quick_switch_.active()) {
        (void)quick_switch_.handle_event(e);
        if (std::string p; quick_switch_.consume_choice(p))
            request(NavKind::ToUnlock, std::move(p));
        return;
    }

    // The import summary is modal: it owns every key until it is dismissed, so
    // a keystroke meant to acknowledge it cannot also act on the list behind it.
    if (!import_summary_.empty()) {
        if (e.type == SDL_EVENT_KEY_DOWN) import_summary_.clear();
        return;
    }

    if (handle_prune_confirm_key(e)) return;

    // Route through template editor and fields form before browse mode
    if (template_editor_.active()) {
        (void)template_editor_.handle_event(e);
        if (!template_editor_.active()) reload();
        return;
    }
    if (fields_form_.active()) {
        (void)fields_form_.handle_event(e);
        if (!fields_form_.active()) {
            SDL_StopTextInput(win_.sdl_window());
            if (fields_form_.consume_saved()) reload();
        }
        return;
    }

    if (filtering_) { handle_filter_event(e); return; }

    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
            handle_key_down_in_browse_mode(e.key);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (const int idx = row_at(e.button.y); idx >= 0) {
                nav_.select(idx);
                open_selected();
            }
            break;
        default: break;
    }
}

void TagOverviewScreen::close_filter()
{
    filter_.clear();
    filtering_ = false;
    SDL_StopTextInput(win_.sdl_window());
    rebuild();
}

void TagOverviewScreen::handle_filter_event(const SDL_Event& e)
{
    // Precedence rule (Phase 54): the filter field owns editing keys while it is
    // open. field_owns_event means a Backspace on an EMPTY filter falls through
    // below and closes filter mode instead of being silently swallowed.
    if (field_owns_event(filter_, e)) {
        const uint64_t rev = filter_.revision();
        if (handle_text_input_event(filter_, e)) {
            if (filter_.revision() != rev) rebuild();
            return;
        }
    }
    if (e.type != SDL_EVENT_KEY_DOWN) return;
    switch (e.key.key) {
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE: close_filter();  break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:  open_selected(); break;
        case SDLK_UP:        nav_.move(-1);   break;
        case SDLK_DOWN:      nav_.move(1);    break;
        default: break;
    }
}

void TagOverviewScreen::render(gfx::Renderer& r)
{
    using namespace gfx::theme;
    const auto W = static_cast<float>(win_.width());
    const auto H = static_cast<float>(win_.height());

    r.draw_text(font_, OX, 40, "Tag Overview", TEXT_DIM);
    const char* sort_label = (sort_ == TagSort::Name) ? "Sort: name (Tab)" : "Sort: count (Tab)";
    r.draw_text(font_, OX, 84, "[F1] Help", TEXT_FAINT);
    r.draw_text(font_, OX, 112, sort_label, TEXT_FAINT);
    if (filtering_ || !filter_.empty()) {
        const float fx = OX + 280;
        const auto  lw = static_cast<float>(font_.measure("Filter: "));
        r.draw_text(font_, fx, 112, "Filter: ", TEXT);
        draw_inline_edit_text(r, font_, fx + lw, 112, W - (fx + lw) - OX,
                              filter_, filter_chrome_);
    }

    // Draw error message if present
    if (!error_.empty())
        r.draw_text(font_, OX, 132, error_, gfx::theme::DANGER);

    if (!shown_.empty()) {
        const auto g = compute_geom(font_.pixel_height(), H, static_cast<int>(shown_.size()),
                                    nav_.selected());
        const float ph = font_.pixel_height();
        // Hoisted: vault_settings returns by value; we take const ref to avoid deep-copy per row.
        const auto& cats = vault::vault_settings(vault_).categories;
        for (int i = g.first; i < g.first + g.visible && i < static_cast<int>(shown_.size()); ++i) {
            const float y = OY + static_cast<float>(i - g.first) * g.row_h;
            const bool sel = (i == nav_.selected());
            draw_tag_row(r, font_, W, y, g.row_h, ph, shown_[i], sel, cats);
        }
    } else {
        // Empty state: show placeholder text
        r.draw_text(font_, OX, OY,
                    all_.empty() ? "No tags in this vault yet."
                                 : "No tags match the filter.",
                    TEXT_DIM);
    }

    quick_switch_.render(r, font_, W, H);

    // Draw panels last (template editor and fields form) — always render on both empty and non-empty paths
    template_editor_.render(r, font_, W, H);
    fields_form_.render(r, font_, W, H);

    render_prune_confirm(r, W, H);
    render_import_summary(r, W, H);
}

void TagOverviewScreen::render_prune_confirm(gfx::Renderer& r, float win_w, float win_h)
{
    using namespace gfx::theme;
    if (!confirm_prune_) return;

    r.draw_rect({0, 0, win_w, win_h}, gfx::Color{8, 9, 12, 255});   // veil

    const float pw = 560;
    const float ph = 160;
    const float px = (win_w - pw) / 2;
    const float py = (win_h - ph) / 2;
    r.draw_round_rect({px, py, pw, ph}, RADIUS, SURFACE);
    r.draw_round_rect({px, py, pw, ph}, RADIUS, ACCENT, /*filled*/ false);

    auto centered = [&](const std::string& s, float y, gfx::Color c) {
        const auto tw = static_cast<float>(font_.measure(s));
        r.draw_text(font_, px + (pw - tw) / 2, y, s, c);
    };

    centered("Remove all junk tags from this vault?", py + 28, TEXT);
    centered("Tags with no readable text (like \"[()]\") are deleted.", py + 64, TEXT_DIM);
    centered("[Esc/N] Cancel        [Y] Remove", py + ph - 50, TEXT_DIM);
}

void TagOverviewScreen::render_import_summary(gfx::Renderer& r, float win_w, float win_h)
{
    using namespace gfx::theme;
    if (import_summary_.empty()) return;

    const float body_line_h = font_.pixel_height() + 8;
    const PromptBoxLayout l = prompt_box_layout({.font_px = font_.pixel_height(),
                                                 .window_w = win_w, .window_h = win_h,
                                                 .input_h = 0.0f,
                                                 .body_lines = static_cast<int>(import_summary_.size()),
                                                 .body_line_h = body_line_h});

    r.draw_round_rect(l.box, RADIUS, SURFACE);
    r.draw_round_rect(l.box, RADIUS, ACCENT, /*filled*/ false);

    r.draw_text(font_, l.box.x + PROMPT_PAD, l.title_y, summary_title_, TEXT);

    float y = l.body_y;
    for (const std::string& line : import_summary_) {
        r.draw_text(font_, l.box.x + PROMPT_PAD, y, line, TEXT_DIM);
        y += body_line_h;
    }
    r.draw_text(font_, l.box.x + PROMPT_PAD, l.hint_y, "Any key to dismiss", TEXT_FAINT);
}

std::vector<HelpGroup> TagOverviewScreen::help_groups() const
{
    return {{"Navigate", {
        {"Up/Down", "Move selection"}, {"Enter", "Open tag"},
        {"/", "Filter tags"}, {"Tab", "Toggle sort (name/count)"},
        {"E", "Edit tag description & fields"},
        {"Ctrl+T", "Edit category templates"},
        {"Ctrl+I", "Import tag dictionary"},
        {"Ctrl+X", "Remove junk tags"},
        {"Esc", "Back"},
    }},
    text_editing_help_group()};
}

} // namespace ui
