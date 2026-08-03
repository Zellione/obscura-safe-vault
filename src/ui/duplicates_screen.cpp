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

// Inspect request keys carry bit 63 so they can never collide with a
// thumbnail key (a raw chunk offset) in the decode worker.
constexpr uint64_t INSPECT_KEY_BIT = uint64_t{1} << 63;

// Format byte count for display
[[nodiscard]] std::string fmt_bytes(uint64_t bytes)
{
    return ui::format_size(bytes);
}

}

void draw_member_tile(gfx::Renderer& r, gfx::FontAtlas& font, DuplicatesScreen& screen,
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
            float tw = 0;
            float th = 0;
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

void draw_group_row(gfx::Renderer& r, gfx::FontAtlas& font, DuplicatesScreen& screen,
                    size_t group_idx, float y, const DupRowLayout& lay)
{
    using namespace gfx::theme;
    const auto& group = screen.review_.groups()[group_idx];

    // Group header line
    std::string kind_str = "Identical";
    if (group.kind == DupGroup::Kind::Similar) {
        kind_str = std::format("Similar ({} bits)", group.distance_bits);
    } else if (group.kind == DupGroup::Kind::SimilarVideo) {
        kind_str = "Similar video";
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

// Draw confirm-skip overlay
void draw_confirm_skip_overlay(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H, const DuplicatesScreen& screen)
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

    const std::string title = std::format("Skip wave {} of {}?", screen.review_.wave_index() + 1, screen.review_.wave_count());
    centered(title, py + 28, TEXT);
    centered(std::format("Discard {} touched marks? Files stay in the vault.", screen.review_.marked_count()),
             py + 58, TEXT_DIM);

    centered("[Enter/Y] skip wave · [Esc/N] stay", py + ph - 50, TEXT_DIM);
}

// Draw inspect overlay
void draw_inspect_overlay(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H, DuplicatesScreen& screen)
{
    using namespace gfx::theme;

    if (!screen.inspect_.key.has_value()) return;

    // Dimmed backdrop
    r.draw_rect({0, 0, W, H}, gfx::Color{8, 9, 12, 200});

    if (screen.inspect_.decoding) {
        // Show "decoding..." message
        const std::string msg = "decoding...";
        const auto tw = static_cast<float>(font.measure(msg));
        r.draw_text(font, (W - tw) / 2, H / 2 - font.pixel_height() / 2, msg, TEXT_DIM);
    } else {
        // Upload the decoded pixels into the OWNED inspect texture on first
        // draw (mirrors FullTexCache::upload; never the shared thumb cache).
        if (!screen.inspect_.tex && screen.inspect_.image) {
            const image::ImageData& img = *screen.inspect_.image;
            SDL_Texture* tex = SDL_CreateTexture(r.sdl(), SDL_PIXELFORMAT_RGB24,
                                                 SDL_TEXTUREACCESS_STATIC,
                                                 img.width, img.height);
            if (tex && !SDL_UpdateTexture(tex, nullptr, img.pixels.data(), img.width * 3)) {
                SDL_DestroyTexture(tex);
                tex = nullptr;
            }
            screen.inspect_.tex = tex;
            screen.inspect_.image.reset();
            if (!tex) {                 // e.g. exceeds the GPU's max texture size
                screen.close_inspect();
                screen.status_ = "Could not display this image (too large?)";
                screen.mark_dirty();
                return;
            }
        }
        if (screen.inspect_.tex) {
            float tw = 0;
            float th = 0;
            SDL_GetTextureSize(screen.inspect_.tex, &tw, &th);
            const SDL_FRect display = fit_rect(tw, th, {40, 40, W - 80, H - 80});
            r.draw_image(screen.inspect_.tex, display);
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

DuplicatesScreen::~DuplicatesScreen()
{
    close_inspect();
}

void DuplicatesScreen::close_inspect()
{
    if (inspect_.tex) SDL_DestroyTexture(inspect_.tex);
    inspect_.tex = nullptr;
    inspect_.image.reset();
    inspect_.key.reset();
    inspect_.decoding = false;
}

// The pending-confirmation overlays and the inspect view own every key while
// up. Returns true when the key was consumed by one of them.
bool consume_overlay_key(DuplicatesScreen& screen, const SDL_KeyboardEvent& key)
{
    const bool accept = key.key == SDLK_RETURN || key.key == SDLK_Y;
    if (screen.confirm_.apply) {
        screen.confirm_.apply = false;
        if (accept) screen.apply_marked_batch();
        screen.mark_dirty();
        return true;
    }
    if (screen.confirm_.leave) {
        screen.confirm_.leave = false;
        if (accept) {
            screen.leave();
            return true;
        }
        screen.mark_dirty();
        return true;
    }
    if (screen.confirm_.skip) {
        screen.confirm_.skip = false;
        if (accept) screen.skip_wave();
        screen.mark_dirty();
        return true;
    }
    if (screen.inspect_.key.has_value()) {   // any key closes the inspect view
        screen.close_inspect();
        screen.mark_dirty();
        return true;
    }
    return false;
}

void handle_review_key(DuplicatesScreen& screen, const SDL_KeyboardEvent& key)
{
    if (consume_overlay_key(screen, key)) return;

    const auto& groups = screen.review_.groups();
    if (groups.empty()) return;

    const bool ctrl = (key.mod & SDL_KMOD_CTRL) != 0;

    switch (key.key) {
        case SDLK_UP:
            screen.follow_focus(-1);
            break;
        case SDLK_DOWN:
            screen.follow_focus(+1);
            break;
        case SDLK_LEFT:
            if (screen.focus_member_ > 0) --screen.focus_member_;
            screen.mark_dirty();
            break;
        case SDLK_RIGHT:
            if (screen.focus_member_ + 1 < groups[screen.focus_group_].members.size())
                ++screen.focus_member_;
            screen.mark_dirty();
            break;
        case SDLK_SPACE:
            if (screen.review_.toggle(screen.focus_group_, screen.focus_member_))
                screen.status_.clear();
            else
                screen.status_ = "At least one copy of each group stays kept";
            screen.mark_dirty();
            break;
        case SDLK_A:
            if (!ctrl) {
                screen.review_.keep_only(screen.focus_group_, screen.focus_member_);
                screen.mark_dirty();
            }
            break;
        case SDLK_RETURN:
            if (!ctrl) {
                screen.request_inspect();
            } else if (screen.review_.can_apply()) {
                screen.confirm_.apply = true;
                screen.status_.clear();
            } else if (!screen.review_.any_marked()) {
                screen.status_ = "Nothing marked for removal";
            } else {
                screen.status_ = "Cannot remove all copies of a group (keep at least one)";
            }
            screen.mark_dirty();
            break;
        case SDLK_ESCAPE:
            // Only user-touched marks are worth a leave-confirm; the untouched
            // defaults are recreated by any rescan.
            if (screen.review_.touched() && screen.review_.any_marked()) {
                screen.confirm_.leave = true;
                screen.status_ = "Unapplied marks — Ctrl+Enter to apply, Esc again to discard";
                screen.mark_dirty();
            } else {
                screen.leave();
            }
            break;
        case SDLK_N:
            if (screen.review_.touched() && screen.review_.any_marked()) {
                screen.confirm_.skip = true;
                screen.status_ = "Skip this wave? Touched marks will be discarded";
            } else {
                screen.skip_wave();
            }
            screen.mark_dirty();
            break;
        default:
            break;
    }
}

namespace {

// Concatenate a member's decrypted payload for inspect: the poster span for a
// video, every data span for an image.
bool read_inspect_payload(const vault::Vault& v, const DupMember& m, crypto::SecureBytes& out)
{
    if (m.is_video) {
        return vault::read_thumb_span(v, m.thumb_offset, m.thumb_length, out)
               == vault::VaultResult::Ok;
    }
    for (const auto& [off, len] : m.data_spans) {
        crypto::SecureBytes span;
        if (vault::read_thumb_span(v, off, len, span) != vault::VaultResult::Ok) return false;
        const size_t old_size = out.size();
        if (!out.resize(old_size + span.size())) return false;
        std::memcpy(out.data() + old_size, span.data(), span.size());
    }
    return true;
}

} // namespace

void DuplicatesScreen::apply_marked_batch()
{
    const auto     doomed       = review_.marked_paths();   // current wave only
    const uint64_t doomed_bytes = review_.marked_bytes();
    vault::RemoveBatchStats stats;
    if (vault::remove_media_batch(vault_, doomed, &stats) != vault::VaultResult::Ok) {
        status_ = "Delete failed — vault unchanged on disk";
        return;
    }
    applied_files_ += stats.removed;
    applied_bytes_ += doomed_bytes;
    ++waves_applied_;
    review_.finish_wave();
    // auto_reclaim_space may have relocated (Windows compact) or freed-for-
    // reuse the surviving chunks: re-read every remaining span from the index,
    // and drop the offset-keyed texture/failure memos before anything fetches.
    refresh_review_members(vault_, review_);
    failed_.clear();
    cache_.clear();
    advance_after_wave();
}

void DuplicatesScreen::skip_wave()
{
    ++waves_skipped_;
    review_.finish_wave();
    advance_after_wave();
}

void DuplicatesScreen::advance_after_wave()
{
    close_inspect();
    focus_group_  = 0;
    focus_member_ = 0;
    scroll_       = 0.0f;
    status_.clear();
    if (review_.groups().empty()) {
        done_summary_ = wave_summary();
        state_ = State::Done;
    }
    mark_dirty();
}

std::string DuplicatesScreen::wave_summary() const
{
    if (applied_files_ == 0) return "No duplicates removed";
    std::string s = std::format("Removed {} files ({}) in {} wave{}",
                                applied_files_, fmt_bytes(applied_bytes_),
                                waves_applied_, waves_applied_ == 1 ? "" : "s");
    if (waves_skipped_ > 0)
        s += std::format(" · {} wave{} skipped", waves_skipped_,
                         waves_skipped_ == 1 ? "" : "s");
    return s;
}

void DuplicatesScreen::follow_focus(int delta)
{
    const size_t last = review_.wave_size() - 1;
    if (delta < 0 && focus_group_ > 0) --focus_group_;
    if (delta > 0 && focus_group_ < last) ++focus_group_;
    focus_member_ = 0;

    // Row height is member-count independent (ui::dup_row_layout).
    const auto lay = dup_row_layout(OX, static_cast<float>(win_.width()) - 2 * OX,
                                    dup_tile_height(static_cast<float>(win_.height())),
                                    font_.pixel_height(), 1);
    if (delta < 0) {
        // Keep the focused group at the top of the scroll area.
        scroll_ = static_cast<float>(focus_group_) * lay.row_h;
    } else {
        // Keep the focused group's full extent (header + tile + text) visible
        // above the footer.
        const float extent = lay.header_h + lay.tile_h + lay.text_h;
        const float bottom = static_cast<float>(win_.height())
                             - dup_footer_height(font_.pixel_height());
        scroll_ = static_cast<float>(focus_group_) * lay.row_h + extent - (bottom - OY);
    }
    scroll_ = std::max(0.0f, scroll_);
    mark_dirty();
}

void DuplicatesScreen::request_inspect()
{
    const auto& groups = review_.groups();
    if (focus_group_ >= groups.size()) return;
    const auto& members = groups[focus_group_].members;
    if (focus_member_ >= members.size()) return;
    const DupMember& member = members[focus_member_];

    if (const bool no_payload = member.is_video ? member.thumb_length == 0
                                                : member.data_spans.empty();
        no_payload) {
        status_ = "Nothing to inspect for this file";
        mark_dirty();
        return;
    }

    // Unique request key: chunk/poster offset with bit 63 set (never collides
    // with a thumbnail key; offset is nonzero after the payload guard).
    const uint64_t base_offset = member.is_video ? member.thumb_offset
                                                 : member.data_spans[0].first;
    const uint64_t inspect_key = base_offset | INSPECT_KEY_BIT;

    crypto::SecureBytes payload;
    if (!read_inspect_payload(vault_, member, payload)) {
        mark_dirty();
        return;
    }
    close_inspect();                 // drop any previous inspect texture
    inspect_.key = inspect_key;
    inspect_.decoding = true;
    worker_.submit(inspect_key, std::move(payload));
    mark_dirty();
}

// Inspect results stay OUT of the shared thumbnail cache (see InspectState).
// A stale result (inspect already closed or replaced) is simply dropped.
void DuplicatesScreen::take_inspect_result(image::DecodeWorker::Result& res)
{
    if (inspect_.key != res.key) return;
    if (res.image) {
        inspect_.image = std::move(*res.image);
    } else {
        close_inspect();                // failed to decode the inspect image
        status_ = "Could not decode this file";
    }
    inspect_.decoding = false;
}

void DuplicatesScreen::pump_decode_results()
{
    while (auto res = worker_.take_result()) {
        if ((res->key & INSPECT_KEY_BIT) != 0) {
            take_inspect_result(*res);
        } else if (res->image) {
            (void)cache_.get_or_upload(res->key, *res->image);
        } else {
            failed_.insert(res->key);   // regular thumbnail gave up
        }
        mark_dirty();
    }
}

void DuplicatesScreen::finish_scan(DupScanOutcome outcome)
{
    if (outcome.cancelled) {
        leave();
        return;
    }
    review_ = DupReview(std::move(outcome.groups));
    skipped_ = outcome.skipped;
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
    applied_files_ = 0;
    applied_bytes_ = 0;
    waves_applied_ = 0;
    waves_skipped_ = 0;
    stale_ = false;      // a fresh scan supersedes any stale results
    auto items = collect_scan_items(vault_);
    job_.start(vault_, std::move(items), perceptual);
    state_ = State::Scanning;
    mark_dirty();
}

void DuplicatesScreen::update(double dt)
{
    (void)dt;
    using enum State;

    // Pump worker results for thumbnail/inspect decoding.
    if (state_ == Review || state_ == Done) pump_decode_results();

    if (state_ != Scanning) return;
    if (job_.active()) {
        mark_dirty();               // progress may have changed
        return;
    }
    if (auto outcome = job_.take_outcome()) finish_scan(std::move(*outcome));
}

void DuplicatesScreen::leave()
{
    request(back_.kind, back_.path, back_.index);
}

void DuplicatesScreen::render(gfx::Renderer& r)
{
    using namespace gfx::theme;
    using enum State;
    const auto W = static_cast<float>(win_.width());
    const auto H = static_cast<float>(win_.height());

    // Screen title; the Review state paints its opaque header band over this.
    r.draw_text(font_, OX, 40, "Duplicates", TEXT_DIM);
    r.draw_text(font_, OX, 84, "[F1] Help", TEXT_FAINT);

    switch (state_) {
        case Choose:   render_choose(r, W, H);   break;
        case Scanning: render_scanning(r, W, H); break;
        case Review:   render_review(r, W, H);   break;
        case Done:
            r.draw_text(font_, OX, OY, done_summary_, TEXT_DIM);
            r.draw_text(font_, OX, H - 40, "[Enter] rescan · [Esc] back", TEXT_FAINT);
            break;
    }

    // Overlays on top.
    if (state_ == Review) {
        if (confirm_.apply) {
            draw_confirm_apply_overlay(r, font_, W, H, *this);
        } else if (confirm_.leave) {
            draw_confirm_leave_overlay(r, font_, W, H, *this);
        } else if (confirm_.skip) {
            draw_confirm_skip_overlay(r, font_, W, H, *this);
        } else if (inspect_.key.has_value()) {
            draw_inspect_overlay(r, font_, W, H, *this);
        }
    }
}

void DuplicatesScreen::render_choose(gfx::Renderer& r, float W, float H)
{
    using namespace gfx::theme;
    const float ph = font_.pixel_height();

    // Two selectable rows: "Exact duplicates" / "Exact + visually similar"
    constexpr float ROW_H = 60.0f;
    constexpr float RADIUS_CHOOSE = 10.0f;

    for (int i = 0; i < 2; ++i) {
        const float y = OY + static_cast<float>(i) * (ROW_H + 12.0f);
        const bool sel = (i == choose_sel_);
        const std::string text = (i == 0) ? "Exact duplicates" : "Exact + visually similar";

        const SDL_FRect row{OX, y, W - 2 * OX, ROW_H};
        if (sel) r.draw_selection_glow(row, RADIUS_CHOOSE, ACCENT);
        r.draw_round_rect(row, RADIUS_CHOOSE, sel ? SURFACE_HI : SURFACE);
        r.draw_round_rect(row, RADIUS_CHOOSE, sel ? ACCENT : BORDER, /*filled*/ false);
        r.draw_text(font_, OX + 20, y + (ROW_H - ph) * 0.5f, text, sel ? TEXT : TEXT_DIM);
    }

    r.draw_text(font_, OX, H - 40, "Up/Down to select, Enter to start, Esc to back", TEXT_FAINT);
}

void DuplicatesScreen::render_scanning(gfx::Renderer& r, float W, float H)
{
    using namespace gfx::theme;

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

    r.draw_round_rect({OX + 20, BAR_Y, BAR_W, BAR_H}, 2, OK);
    r.draw_round_rect({OX + 20, BAR_Y, BAR_W * progress, BAR_H}, 2, ACCENT);

    r.draw_text(font_, OX + 20, BAR_Y + BAR_H + 16, "Scanning...", TEXT_DIM);
    r.draw_text(font_, OX + 20, BAR_Y + BAR_H + 40, fit_text(font_, current, BAR_W), TEXT);
    r.draw_text(font_, OX + 20, BAR_Y + BAR_H + 64, status, TEXT_FAINT);

    r.draw_text(font_, OX, H - 40, "Esc to cancel", TEXT_FAINT);
}

void DuplicatesScreen::render_review(gfx::Renderer& r, float W, float H)
{
    using namespace gfx::theme;

    // Scrolled group rows first; the opaque chrome bands paint over them
    // afterwards (reserve, never overlay — same rule as the gallery grid).
    // Row height is member-count independent, so one group's layout differs
    // from the next only in tile width/centering.

    // Viewport culling: row_h is member-count independent (dup_layout.h), so
    // the visible index range is direct arithmetic. Off-screen rows are never
    // drawn — and since draw_member_tile is what submits thumbnail fetches,
    // they cost no decode work either (the 8 GB review-churn fix).
    const float tile_h = dup_tile_height(H);
    const float row_h =
        dup_row_layout(OX, W - 2 * OX, tile_h, font_.pixel_height(), 1).row_h;
    const float view_h = H - dup_footer_height(font_.pixel_height()) - OY;
    const size_t n = review_.wave_size();
    const auto first = std::min(n, static_cast<size_t>(std::max(0.0f, scroll_ / row_h)));
    const auto last  = std::min(n, static_cast<size_t>((scroll_ + view_h) / row_h) + 1);
    for (size_t g = first; g < last; ++g) {
        const auto lay = dup_row_layout(OX, W - 2 * OX, tile_h, font_.pixel_height(),
                                        review_.groups()[g].members.size());
        draw_group_row(r, font_, *this, g, OY + static_cast<float>(g) * row_h - scroll_, lay);
    }

    // Prune queued decodes that scrolled out of view (image_viewer.cpp:850
    // pattern); keep the inspect request alive.
    std::vector<uint64_t> keep;
    for (size_t g = first; g < last; ++g)
        for (const DupMember& m : review_.groups()[g].members)
            if (m.thumb_length > 0) keep.push_back(m.thumb_offset);
    if (inspect_.key.has_value()) keep.push_back(*inspect_.key);
    worker_.retain(keep);

    // Opaque header band: title + skipped notice + stale warning.
    draw_chrome_band(r, {0, 0, W, OY - 8}, BG, /*rule_at_bottom*/ true);

    size_t   wave_files       = 0;
    uint64_t wave_reclaimable = 0;
    for (size_t g = 0; g < review_.wave_size(); ++g) {
        wave_files       += review_.groups()[g].members.size();
        wave_reclaimable += group_reclaimable(review_.groups()[g]);
    }
    const size_t later = review_.groups().size() - review_.wave_size();
    const std::string header = std::format(
        "Duplicates — wave {}/{} · {} groups · {} files · {} reclaimable{}",
        review_.wave_index() + 1, review_.wave_count(), review_.wave_size(),
        wave_files, fmt_bytes(wave_reclaimable),
        later > 0 ? std::format(" · {} groups in later waves", later) : "");
    r.draw_text(font_, OX, 40, header, TEXT_DIM);

    if (skipped_ > 0) {
        r.draw_text(font_, OX, 84, std::format("couldn't examine {} files", skipped_), TEXT_FAINT);
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

    r.draw_text(font_, OX, footer_y + pitch * 2,
                "[Space] keep/remove  [A] keep only  [Ctrl+Enter] apply wave  [N] skip wave  [Esc] back",
                TEXT_FAINT);
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
                    {"Ctrl+Enter", "Apply this wave's marks"},
                    {"N", "Skip this wave (no changes)"},
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
