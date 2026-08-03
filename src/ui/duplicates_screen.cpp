#include "ui/duplicates_screen.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <string>

#include "crypto/secure_mem.h"
#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/texture_cache.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/dup_layout.h"
#include "ui/meta_format.h"
#include "ui/text_metrics.h"
#include "ui/widgets.h"
#include "vault/vault.h"

namespace ui {

namespace {
constexpr float OX  = 40;    // left margin
constexpr float OY  = 150;   // list top
// Tile/row geometry lives in ui/dup_layout.* (pure, unit-tested): tiles share
// the full content width, scale down with member count, and row heights are
// font-derived so text can never bleed into the next group's header.
constexpr float BADGE_SIZE = 24.0f;    // keep/remove badge size
constexpr float RADIUS = 8.0f;         // border radius

// Format byte count for display
[[nodiscard]] std::string fmt_bytes(uint64_t bytes)
{
    return ui::format_size(bytes);
}

}

void draw_member_tile(gfx::Renderer& r, gfx::FontAtlas& font, const DuplicatesScreen& screen,
                      const DupMember& member, bool focused, const SDL_FRect& tile_rect)
{
    using namespace gfx::theme;

    // Draw tile background
    r.draw_rect(tile_rect, gfx::Color{0, 0, 0, 255});

    // Draw thumbnail or placeholder
    const uint64_t key = member.thumb_offset;
    if (member.thumb_length == 0) {
        // No thumbnail available
        r.draw_text(font, tile_rect.x + 6, tile_rect.y + tile_rect.h * 0.5f - 14,
                    "(no thumb)", TEXT_DIM);
    } else if (!screen.failed_.contains(key)) {
        // Try to get texture from cache or submit fetch
        if (SDL_Texture* tex = screen.cache_.get(key)) {
            float tw = 0, th = 0;
            SDL_GetTextureSize(tex, &tw, &th);
            r.draw_image(tex, fit_rect(tw, th, tile_rect));
        } else if (!screen.worker_.pending(key)) {
            // Submit fetch for this thumbnail
            screen.worker_.submit_fetch(key,
                [&v = screen.vault_, off = member.thumb_offset, len = member.thumb_length](crypto::SecureBytes& out) {
                    return vault::read_thumb_span(v, off, len, out) == vault::VaultResult::Ok;
                });
        }
    }

    // Draw video badge if needed
    if (member.is_video) {
        const float cx = tile_rect.x + tile_rect.w * 0.5f;
        const float cy = tile_rect.y + tile_rect.h * 0.5f;
        const float s  = std::min(tile_rect.w, tile_rect.h) * 0.16f;
        const SDL_FPoint a{cx - s * 0.5f, cy - s};
        const SDL_FPoint b{cx - s * 0.5f, cy + s};
        const SDL_FPoint c{cx + s, cy};
        r.draw_triangle({a.x + 2, a.y + 2}, {b.x + 2, b.y + 2}, {c.x + 2, c.y + 2},
                        gfx::Color{0, 0, 0, 255});
        r.draw_triangle(a, b, c, gfx::Color{255, 255, 255, 255});
    }

    // Draw focused indicator and border
    if (focused) {
        r.draw_selection_glow(tile_rect, 4, ACCENT);
        r.draw_round_rect(tile_rect, 4, ACCENT, /*filled*/ false);
    }

    // Draw keep/remove badge (top-left)
    const SDL_FRect badge{tile_rect.x + 4, tile_rect.y + 4, BADGE_SIZE, BADGE_SIZE};
    const gfx::Color badge_color = member.keep ? OK : DANGER;
    const gfx::Color badge_text = member.keep ? gfx::Color{0, 0, 0, 255} : gfx::Color{255, 255, 255, 255};
    r.draw_round_rect(badge, 2, badge_color);
    r.draw_text(font, badge.x + 2, badge.y + 2,
                member.keep ? "K" : "R", badge_text);

    // Draw text below tile (inside the layout's text_h band)
    const float text_y = tile_rect.y + tile_rect.h + 4;
    const float text_w = tile_rect.w - 4;
    const float pitch = line_pitch(font.pixel_height());

    // Name line (truncate if needed)
    const std::string name_text = fit_text(font, member.name, text_w);
    r.draw_text(font, tile_rect.x + 2, text_y, name_text, TEXT);

    // Parent path line
    const std::string parent_text = member.parent_path.empty() ? "/" : member.parent_path;
    const std::string parent_display = fit_text(font, parent_text, text_w);
    r.draw_text(font, tile_rect.x + 2, text_y + pitch, parent_display, TEXT_DIM);

    // Dimensions and size line
    const std::string dim_text = std::format("{}x{} · {}", member.width, member.height, fmt_bytes(member.bytes));
    const std::string dim_display = fit_text(font, dim_text, text_w);
    r.draw_text(font, tile_rect.x + 2, text_y + pitch * 2, dim_display, TEXT_FAINT);
}

void draw_group_row(gfx::Renderer& r, gfx::FontAtlas& font, const DuplicatesScreen& screen,
                    size_t group_idx, float y, const DupRowLayout& lay)
{
    using namespace gfx::theme;
    const auto& group = screen.review_.groups()[group_idx];

    // Group header line
    std::string kind_str = (group.kind == DupGroup::Kind::Identical) ? "Identical" : "Similar";
    if (group.kind == DupGroup::Kind::Similar) {
        kind_str = std::format("Similar ({} bits)", group.distance_bits);
    }
    const uint64_t reclaimable = group_reclaimable(group);
    const std::string header = std::format("{} · {} files · {} reclaimable",
                                           kind_str, group.members.size(), fmt_bytes(reclaimable));

    const gfx::Color header_color = screen.review_.group_all_removed(group_idx) ? DANGER : TEXT_DIM;
    r.draw_text(font, OX, y, header, header_color);

    // Member tiles: centered band across the full content width.
    float tile_x = lay.first_x;
    for (size_t m = 0; m < group.members.size(); ++m) {
        const auto& member = group.members[m];
        const bool focused = (group_idx == screen.focus_group_ && m == screen.focus_member_);

        const SDL_FRect tile_rect{tile_x, y + lay.header_h, lay.tile_w, lay.tile_h};
        draw_member_tile(r, font, screen, member, focused, tile_rect);

        tile_x += lay.tile_w + DUP_TILE_GAP;
    }
}

// Draw confirm-apply overlay
void draw_confirm_apply_overlay(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H, const DuplicatesScreen& screen)
{
    using namespace gfx::theme;

    // Veil the whole window
    r.draw_rect({0, 0, W, H}, gfx::Color{8, 9, 12, 255});

    const float pw = 560;
    const float ph = 230;
    const float px = (W - pw) / 2;
    const float py = (H - ph) / 2;
    r.draw_round_rect({px, py, pw, ph}, 8, SURFACE);
    r.draw_round_rect({px, py, pw, ph}, 8, BORDER, /*filled*/ false);

    auto centered = [&](const std::string& s, float y, gfx::Color c) {
        const auto tw = static_cast<float>(font.measure(s));
        r.draw_text(font, px + (pw - tw) / 2, y, s, c);
    };

    const std::string title = std::format("Delete {} files ({})?",
        screen.review_.marked_count(), fmt_bytes(screen.review_.marked_bytes()));
    centered(title, py + 28, TEXT);
    centered("This cannot be undone.", py + 58, TEXT_DIM);

    centered("[Enter/Y] delete · [Esc/N] cancel", py + ph - 50, TEXT_DIM);
}

// Draw confirm-leave overlay
void draw_confirm_leave_overlay(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H, const DuplicatesScreen& screen)
{
    using namespace gfx::theme;

    // Veil the whole window
    r.draw_rect({0, 0, W, H}, gfx::Color{8, 9, 12, 255});

    const float pw = 560;
    const float ph = 230;
    const float px = (W - pw) / 2;
    const float py = (H - ph) / 2;
    r.draw_round_rect({px, py, pw, ph}, 8, SURFACE);
    r.draw_round_rect({px, py, pw, ph}, 8, BORDER, /*filled*/ false);

    auto centered = [&](const std::string& s, float y, gfx::Color c) {
        const auto tw = static_cast<float>(font.measure(s));
        r.draw_text(font, px + (pw - tw) / 2, y, s, c);
    };

    const std::string title = std::format("Discard {} unapplied marks?", screen.review_.marked_count());
    centered(title, py + 28, TEXT);

    centered("[Enter/Y] leave · [Esc/N] stay", py + ph - 50, TEXT_DIM);
}

// Draw inspect overlay
void draw_inspect_overlay(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H, const DuplicatesScreen& screen)
{
    using namespace gfx::theme;

    if (!screen.inspect_) return;

    // Dimmed backdrop
    r.draw_rect({0, 0, W, H}, gfx::Color{8, 9, 12, 200});

    if (screen.inspect_decoding_) {
        // Show "decoding..." message
        const std::string msg = "decoding...";
        const auto tw = static_cast<float>(font.measure(msg));
        r.draw_text(font, (W - tw) / 2, H / 2 - font.pixel_height() / 2, msg, TEXT_DIM);
    } else {
        // Try to render the inspect texture
        SDL_Texture* tex = screen.cache_.get(*screen.inspect_);
        if (tex) {
            float tw = 0, th = 0;
            SDL_GetTextureSize(tex, &tw, &th);
            const SDL_FRect display = fit_rect(tw, th, {40, 40, W - 80, H - 80});
            r.draw_image(tex, display);
        }
    }

    // Hint at bottom
    const std::string hint = "Any key to close";
    const auto tw = static_cast<float>(font.measure(hint));
    r.draw_text(font, (W - tw) / 2, H - 40, hint, TEXT_FAINT);
}


DuplicatesScreen::DuplicatesScreen(gfx::Window& win, gfx::FontAtlas& font,
                                   vault::Vault& vault, gfx::TextureCache& cache, Nav back)
    : win_(win), font_(font), vault_(vault), cache_(cache), back_(std::move(back))
{
}

void handle_review_key(DuplicatesScreen& screen, const SDL_KeyboardEvent& key)
{
    // Handle confirm-apply overlay
    if (screen.confirm_apply_) {
        if (key.key == SDLK_RETURN || key.key == SDLK_Y) {
            // Apply the deletion
            const auto doomed = screen.review_.marked_paths();
            vault::RemoveBatchStats stats;
            const auto res = vault::remove_media_batch(screen.vault_, doomed, &stats);
            screen.confirm_apply_ = false;
            if (res == vault::VaultResult::Ok) {
                screen.done_summary_ = std::format("Removed {} files ({}){}",
                    stats.removed,
                    fmt_bytes(screen.review_.marked_bytes()),
                    stats.missing ? std::format(" — {} already gone", stats.missing) : "");
                screen.state_ = DuplicatesScreen::State::Done;
            } else {
                screen.status_ = "Delete failed — vault unchanged on disk";
            }
            screen.mark_dirty();
        } else {
            // Any other key cancels
            screen.confirm_apply_ = false;
            screen.mark_dirty();
        }
        return;
    }

    // Handle confirm-leave overlay
    if (screen.confirm_leave_) {
        if (key.key == SDLK_RETURN || key.key == SDLK_Y) {
            // Leave
            screen.confirm_leave_ = false;
            screen.leave();
        } else {
            // Any other key cancels the leave
            screen.confirm_leave_ = false;
            screen.mark_dirty();
        }
        return;
    }

    // Handle inspect view
    if (screen.inspect_) {
        screen.inspect_.reset();
        screen.inspect_decoding_ = false;
        screen.mark_dirty();
        return;
    }

    const auto& groups = screen.review_.groups();
    if (groups.empty()) return;

    const bool ctrl = (key.mod & SDL_KMOD_CTRL) != 0;

    switch (key.key) {
        case SDLK_UP: {
            if (screen.focus_group_ > 0) {
                --screen.focus_group_;
            }
            screen.focus_member_ = 0;
            // Scroll to keep the focused group at the top of the scroll area.
            // Row height is member-count independent (ui::dup_row_layout).
            const auto lay = dup_row_layout(OX, static_cast<float>(screen.win_.width()) - 2 * OX,
                                            dup_tile_height(static_cast<float>(screen.win_.height())),
                                            screen.font_.pixel_height(), 1);
            screen.scroll_ = static_cast<float>(screen.focus_group_) * lay.row_h;
            screen.scroll_ = std::max(0.0f, screen.scroll_);
            screen.mark_dirty();
            break;
        }
        case SDLK_DOWN: {
            if (screen.focus_group_ < groups.size() - 1) {
                ++screen.focus_group_;
            }
            screen.focus_member_ = 0;
            // Scroll to keep the focused group's full extent (header + tile +
            // text) visible above the footer.
            const auto lay = dup_row_layout(OX, static_cast<float>(screen.win_.width()) - 2 * OX,
                                            dup_tile_height(static_cast<float>(screen.win_.height())),
                                            screen.font_.pixel_height(), 1);
            const float extent = lay.header_h + lay.tile_h + lay.text_h;
            const float screen_bottom = static_cast<float>(screen.win_.height())
                                        - dup_footer_height(screen.font_.pixel_height());
            screen.scroll_ = static_cast<float>(screen.focus_group_) * lay.row_h + extent - (screen_bottom - OY);
            screen.scroll_ = std::max(0.0f, screen.scroll_);
            screen.mark_dirty();
            break;
        }
        case SDLK_LEFT: {
            if (screen.focus_member_ > 0) {
                --screen.focus_member_;
            }
            screen.mark_dirty();
            break;
        }
        case SDLK_RIGHT: {
            if (!groups.empty() && screen.focus_group_ < groups.size()) {
                const auto& members = groups[screen.focus_group_].members;
                if (screen.focus_member_ < members.size() - 1) {
                    ++screen.focus_member_;
                }
            }
            screen.mark_dirty();
            break;
        }
        case SDLK_SPACE: {
            if (screen.focus_group_ < groups.size()) {
                screen.review_.toggle(screen.focus_group_, screen.focus_member_);
                screen.mark_dirty();
            }
            break;
        }
        case SDLK_A: {
            if (!ctrl && screen.focus_group_ < groups.size()) {
                screen.review_.keep_only(screen.focus_group_, screen.focus_member_);
                screen.mark_dirty();
            }
            break;
        }
        case SDLK_RETURN: {
            if (ctrl) {
                if (screen.review_.can_apply()) {
                    screen.confirm_apply_ = true;
                    screen.status_.clear();
                } else {
                    // Check why can_apply failed
                    if (!screen.review_.any_marked()) {
                        screen.status_ = "Nothing marked for removal";
                    } else {
                        // At least one group has all copies marked for removal
                        screen.status_ = "Cannot remove all copies of a group (keep at least one)";
                    }
                }
                screen.mark_dirty();
            } else {
                // Enter without Ctrl: start inspect
                const auto& inspect_groups = screen.review_.groups();
                if (!inspect_groups.empty() && screen.focus_group_ < inspect_groups.size()) {
                    const auto& group = inspect_groups[screen.focus_group_];
                    if (screen.focus_member_ < group.members.size()) {
                        const auto& member = group.members[screen.focus_member_];

                        // Guard 1: Videos require a poster to inspect
                        if (member.is_video && member.thumb_length == 0) {
                            screen.status_ = "Nothing to inspect for this file";
                            screen.mark_dirty();
                            break;
                        }

                        // Guard 2: Images require data_spans to inspect
                        if (!member.is_video && member.data_spans.empty()) {
                            screen.status_ = "Nothing to inspect for this file";
                            screen.mark_dirty();
                            break;
                        }

                        // Compute unique inspect key: use first data span offset (guaranteed nonzero after guards)
                        uint64_t base_offset = 0;
                        if (member.is_video) {
                            base_offset = member.thumb_offset;
                        } else {
                            base_offset = member.data_spans[0].first;  // first chunk offset
                        }
                        const uint64_t inspect_key = base_offset | (uint64_t{1} << 63);

                        screen.inspect_ = inspect_key;
                        screen.inspect_decoding_ = true;

                        // For videos, use poster span; for images, concatenate all data_spans
                        crypto::SecureBytes data_to_decode;
                        if (member.is_video) {
                            // Use poster span
                            const auto res = vault::read_thumb_span(screen.vault_, member.thumb_offset, member.thumb_length, data_to_decode);
                            if (res != vault::VaultResult::Ok) {
                                screen.inspect_.reset();
                                screen.inspect_decoding_ = false;
                            }
                        } else {
                            // Concatenate all data_spans
                            for (const auto& [off, len] : member.data_spans) {
                                crypto::SecureBytes span;
                                const auto res = vault::read_thumb_span(screen.vault_, off, len, span);
                                if (res != vault::VaultResult::Ok) {
                                    screen.inspect_.reset();
                                    screen.inspect_decoding_ = false;
                                    break;
                                }
                                // Append to data_to_decode
                                const size_t old_size = data_to_decode.size();
                                if (!data_to_decode.resize(old_size + span.size())) {
                                    screen.inspect_.reset();
                                    screen.inspect_decoding_ = false;
                                    break;
                                }
                                std::memcpy(data_to_decode.data() + old_size, span.data(), span.size());
                            }
                        }

                        if (screen.inspect_) {
                            // Submit to worker
                            screen.worker_.submit(inspect_key, std::move(data_to_decode));
                        }
                    }
                }
                screen.mark_dirty();
            }
            break;
        }
        case SDLK_ESCAPE: {
            if (screen.review_.any_marked()) {
                screen.confirm_leave_ = true;
                screen.status_ = "Unapplied marks — Ctrl+Enter to apply, Esc again to discard";
            } else {
                screen.leave();
            }
            screen.mark_dirty();
            break;
        }
        default:
            break;
    }
}

void DuplicatesScreen::handle_key(const SDL_KeyboardEvent& key)
{
    switch (state_) {
        case State::Choose:
            switch (key.key) {
                case SDLK_UP:
                    choose_sel_ = std::max(0, choose_sel_ - 1);
                    mark_dirty();
                    break;
                case SDLK_DOWN:
                    choose_sel_ = std::min(1, choose_sel_ + 1);
                    mark_dirty();
                    break;
                case SDLK_RETURN:
                case SDLK_SPACE:
                    start_scan(choose_sel_ == 1);
                    break;
                case SDLK_ESCAPE:
                    leave();
                    break;
                default:
                    break;
            }
            break;

        case State::Scanning:
            if (key.key == SDLK_ESCAPE) {
                job_.cancel();
                mark_dirty();
            }
            break;

        case State::Review:
            handle_review_key(*this, key);
            break;

        case State::Done:
            switch (key.key) {
                case SDLK_RETURN:
                    // Rescan
                    state_ = State::Choose;
                    choose_sel_ = 0;
                    focus_group_ = 0;
                    focus_member_ = 0;
                    scroll_ = 0.0f;
                    status_.clear();
                    done_summary_.clear();
                    mark_dirty();
                    break;
                case SDLK_ESCAPE:
                    leave();
                    break;
                default:
                    break;
            }
            break;

        default:
            break;
    }
}

void DuplicatesScreen::handle_event(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_KEY_DOWN) {
        handle_key(e.key);
    }
}

void DuplicatesScreen::start_scan(bool perceptual)
{
    auto items = collect_scan_items(vault_);
    job_.start(vault_, std::move(items), perceptual);
    state_ = State::Scanning;
    mark_dirty();
}

void DuplicatesScreen::update(double dt)
{
    (void)dt;

    // Pump worker results for thumbnail decoding (any state that shows thumbnails)
    if (state_ == State::Review || state_ == State::Done) {
        while (auto res = worker_.take_result()) {
            if (inspect_ && res->key == *inspect_) {
                // This is an inspect result
                if (res->image) {
                    (void)cache_.get_or_upload(res->key, *res->image);
                    inspect_decoding_ = false;
                } else {
                    // Failed to decode inspect image
                    inspect_.reset();
                    inspect_decoding_ = false;
                }
            } else {
                // Regular thumbnail result
                if (res->image) {
                    (void)cache_.get_or_upload(res->key, *res->image);
                } else {
                    failed_.insert(res->key);
                }
            }
            mark_dirty();
        }
    }

    if (state_ != State::Scanning) return;

    if (!job_.active()) {
        // Scan finished
        if (auto outcome = job_.take_outcome()) {
            if (outcome->cancelled) {
                leave();
            } else {
                review_ = DupReview(std::move(outcome->groups));
                skipped_ = outcome->skipped;
                focus_group_ = 0;
                focus_member_ = 0;
                scroll_ = 0.0f;
                status_.clear();
                if (review_.groups().empty() && skipped_ == 0) {
                    done_summary_ = "No duplicates found";
                    state_ = State::Done;
                } else {
                    state_ = State::Review;
                }
                mark_dirty();
            }
        }
    } else {
        // Still scanning - mark dirty if progress changed
        mark_dirty();
    }
}

void DuplicatesScreen::leave()
{
    request(back_.kind, back_.path, back_.index);
}

void DuplicatesScreen::render(gfx::Renderer& r)
{
    using namespace gfx::theme;
    const auto  W  = static_cast<float>(win_.width());
    const auto  H  = static_cast<float>(win_.height());
    const float ph = font_.pixel_height();

    // Header band: "Duplicates" + "[F1] Help"
    r.draw_text(font_, OX, 40, "Duplicates", TEXT_DIM);
    r.draw_text(font_, OX, 84, "[F1] Help", TEXT_FAINT);

    if (state_ == State::Choose) {
        // Two selectable rows: "Exact duplicates" / "Exact + visually similar"
        constexpr float ROW_H = 60.0f;
        constexpr float RADIUS_CHOOSE = 10.0f;

        const std::string row1_text = "Exact duplicates";
        const std::string row2_text = "Exact + visually similar";

        for (int i = 0; i < 2; ++i) {
            const float y = OY + static_cast<float>(i) * (ROW_H + 12.0f);
            const bool sel = (i == choose_sel_);
            const std::string& text = (i == 0) ? row1_text : row2_text;

            const SDL_FRect row{OX, y, W - 2 * OX, ROW_H};

            // Draw selection glow and background
            if (sel) r.draw_selection_glow(row, RADIUS_CHOOSE, ACCENT);
            r.draw_round_rect(row, RADIUS_CHOOSE, sel ? SURFACE_HI : SURFACE);
            r.draw_round_rect(row, RADIUS_CHOOSE, sel ? ACCENT : BORDER, /*filled*/ false);

            // Text
            r.draw_text(font_, OX + 20, y + (ROW_H - ph) * 0.5f, text,
                       sel ? TEXT : TEXT_DIM);
        }

        // Hint at bottom
        const std::string hint = "Up/Down to select, Enter to start, Esc to back";
        r.draw_text(font_, OX, H - 40, hint, TEXT_FAINT);
    } else if (state_ == State::Scanning) {
        // Progress bar like import_status_screen
        constexpr float BAR_H = 12.0f;
        constexpr float BAR_Y = OY + 20.0f;
        const float BAR_W = W - 2 * OX - 40;

        const size_t done = job_.progress_done();
        const size_t total = job_.progress_total();
        const float progress = total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 0.0f;

        const std::string current = job_.current_name();
        const std::string status = total > 0
            ? std::format("{} / {} items", done, total)
            : "Initializing...";

        // Background bar
        r.draw_round_rect({OX + 20, BAR_Y, BAR_W, BAR_H}, 2, OK);
        // Progress bar
        r.draw_round_rect({OX + 20, BAR_Y, BAR_W * progress, BAR_H}, 2, ACCENT);

        // Status text
        r.draw_text(font_, OX + 20, BAR_Y + BAR_H + 16, "Scanning...", TEXT_DIM);
        r.draw_text(font_, OX + 20, BAR_Y + BAR_H + 40, fit_text(font_, current, BAR_W), TEXT);
        r.draw_text(font_, OX + 20, BAR_Y + BAR_H + 64, status, TEXT_FAINT);

        const std::string cancel_hint = "Esc to cancel";
        r.draw_text(font_, OX, H - 40, cancel_hint, TEXT_FAINT);
    } else if (state_ == State::Review) {
        // Scrolled group rows first; the opaque chrome bands paint over them
        // afterwards (reserve, never overlay — same rule as the gallery grid).
        // Row height is member-count independent, so one group's layout
        // differs from the next only in tile width/centering.
        const float tile_h = dup_tile_height(H);
        float group_y = OY;
        for (size_t g = 0; g < review_.groups().size(); ++g) {
            const auto lay = dup_row_layout(OX, W - 2 * OX, tile_h, font_.pixel_height(),
                                            review_.groups()[g].members.size());
            draw_group_row(r, font_, *this, g, group_y - scroll_, lay);
            group_y += lay.row_h;
        }

        // Opaque header band: title + skipped notice + stale warning.
        draw_chrome_band(r, {0, 0, W, OY - 8}, BG, /*rule_at_bottom*/ true);

        const size_t total_groups = review_.groups().size();
        size_t total_files = 0;
        uint64_t total_reclaimable = 0;
        for (const auto& g : review_.groups()) {
            total_files += g.members.size();
            total_reclaimable += group_reclaimable(g);
        }
        const std::string header = std::format("Duplicates — {} groups · {} files · {} reclaimable",
                                               total_groups, total_files, fmt_bytes(total_reclaimable));
        r.draw_text(font_, OX, 40, header, TEXT_DIM);

        if (skipped_ > 0) {
            const std::string skipped_text = std::format("couldn't examine {} files", skipped_);
            r.draw_text(font_, OX, 84, skipped_text, TEXT_FAINT);
        }

        if (stale_) {
            constexpr float BANNER_H = 30.0f;
            const SDL_FRect stale_banner{OX, OY - 40, W - 2 * OX, BANNER_H};
            r.draw_round_rect(stale_banner, RADIUS, DANGER);
            r.draw_text(font_, OX + 8, OY - 35, "Vault changed — results may be stale (rescan with Esc, Ctrl+D)",
                       TEXT);
        }

        // Opaque footer band: status + marked totals + key hints, one
        // font-derived line each.
        const float pitch = line_pitch(font_.pixel_height());
        const float footer_h = dup_footer_height(font_.pixel_height());
        draw_chrome_band(r, {0, H - footer_h, W, footer_h}, BG, /*rule_at_bottom*/ false);

        const float footer_y = H - footer_h + 6;
        if (!status_.empty()) {
            r.draw_text(font_, OX, footer_y, status_, DANGER);
        }
        const std::string marked_text = std::format("{} files marked · {}",
                                                    review_.marked_count(), fmt_bytes(review_.marked_bytes()));
        r.draw_text(font_, OX, footer_y + pitch, marked_text, TEXT_DIM);

        const std::string hints = "[Space] keep/remove  [A] keep only  [Ctrl+Enter] apply  [Esc] back";
        r.draw_text(font_, OX, footer_y + pitch * 2, hints, TEXT_FAINT);
    } else if (state_ == State::Done) {
        // Done state
        r.draw_text(font_, OX, OY, done_summary_, TEXT_DIM);
        r.draw_text(font_, OX, H - 40, "[Enter] rescan · [Esc] back", TEXT_FAINT);
    }

    // Draw overlays on top
    if (state_ == State::Review) {
        if (confirm_apply_) {
            draw_confirm_apply_overlay(r, font_, W, H, *this);
        } else if (confirm_leave_) {
            draw_confirm_leave_overlay(r, font_, W, H, *this);
        } else if (inspect_) {
            draw_inspect_overlay(r, font_, W, H, *this);
        }
    }
}

std::vector<HelpGroup> DuplicatesScreen::help_groups() const
{
    switch (state_) {
        case State::Choose:
            return {
                {"Scan", {
                    {"Up/Down", "Select scan mode"},
                    {"Enter", "Start scan"},
                    {"Esc", "Cancel"},
                }},
            };

        case State::Scanning:
            return {
                {"Scan", {
                    {"Esc", "Cancel scan"},
                }},
            };

        case State::Review:
            return {
                {"Review", {
                    {"Up/Down", "Focus group"},
                    {"Left/Right", "Focus member"},
                    {"Space", "Toggle keep/remove"},
                    {"A", "Keep only this member"},
                    {"Enter", "Inspect full original"},
                    {"Ctrl+Enter", "Apply marks"},
                    {"Esc", "Back (or confirm leave if marks)"},
                }},
            };

        case State::Done:
            return {
                {"Done", {
                    {"Enter", "Rescan"},
                    {"Esc", "Back"},
                }},
            };

        default:
            return {};
    }
}

void DuplicatesScreen::on_vault_changed()
{
    stale_ = true;
    mark_dirty();
}

} // namespace ui
