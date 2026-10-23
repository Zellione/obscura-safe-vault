#include "ui/bulk_rename.h"
#include <algorithm>
#include <cassert>
#include <format>
#include <stdexcept>
#include <system_error>
#include <utility>
#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "platform/error_log.h"
#include "ui/import_queue.h"
#include "ui/progress_modal.h"
#include "ui/text_input_event.h"
#include "ui/text_metrics.h"

namespace ui {
BulkRename::~BulkRename()
{
    close();
}
void BulkRename::open(std::vector<vault::RenameId> ids)
{
    close();
    active_ = true;
    completed_ = false;
    error_.clear();
    if (queue_.busy() || vault::vault_is_read_only(vault_)) {
        error_ = "Wait for imports to finish; the vault must be writable.";
        return;
    }
    queue_.set_exclusive(true);
    exclusive_ = true;
    ids_ = std::move(ids);
    options_ = {};
    pattern_.clear();
    replacement_.clear();
    second_field_ = false;
    details_ = false;
    preview_ready_ = false;
    first_row_ = 0;
    SDL_StartTextInput(win_.sdl_window());
}
void BulkRename::close()
{
    if (busy()) {
        progress_.cancel = true;
        (void)finish_work();
    }
    if (exclusive_) queue_.set_exclusive(false);
    exclusive_ = false;
    if (active_) SDL_StopTextInput(win_.sdl_window());
    active_ = false;
    preview_ = {};
    ids_.clear();
    pattern_.clear();
    replacement_.clear();
}
void BulkRename::launch(std::function<void()> task)
{
    assert(!busy());
    work_.done = false;
    work_.failure = WorkFailure::None;
    progress_.cancel = false;
    try {
        work_.thread = std::jthread([this, task = std::move(task)] {
            // Allocation-free failure transport. Only the owning UI thread
            // reads failure, after joining; no exception text crosses threads.
            try {
                task();
            } catch (const std::bad_alloc&) {
                work_.failure = WorkFailure::Memory;
            } catch (const std::length_error&) {
                work_.failure = WorkFailure::Size;
            } catch (const std::system_error&) {
                work_.failure = WorkFailure::System;
            } catch (const std::runtime_error&) {
                work_.failure = WorkFailure::Runtime;
            } catch (const std::out_of_range&) {
                work_.failure = WorkFailure::Bounds;
            }
            work_.done = true;
        });
    } catch (const std::bad_alloc&) {
        error_ = "Not enough memory to start rename operation.";
        platform::log_error("BulkRename", "Worker allocation failed.");
    } catch (const std::system_error&) {
        error_ = "Could not start rename worker.";
        platform::log_error("BulkRename", "Could not start worker.");
    }
}
bool BulkRename::finish_work()
{
    // Even a published done flag precedes destruction of the task's captures.
    // Join before returning tree ownership or freeing modal data they can use.
    work_.thread.join();
    if (work_.failure == WorkFailure::None) return true;
    error_ = "Operation failed.";
    preview_ready_ = false;
    platform::log_error("BulkRename", "Worker failed; result consumed before releasing ownership.");
    return false;
}
void BulkRename::preview()
{
    options_.pattern = pattern_.text_view();
    options_.replacement = replacement_.text_view();
    progress_.cancel = false;
    progress_.done = 0;
    progress_.total = 0;
    applying_ = false;
    preview_ready_ = false;
    first_row_ = 0;
    error_.clear();
    launch([this] { preview_ = vault::preview_rename(vault_, ids_, options_, &progress_); });
}
void BulkRename::apply()
{
    progress_.cancel = false;
    progress_.done = 0;
    progress_.total = 0;
    applying_ = true;
    launch([this] { result_ = vault::apply_rename(vault_, preview_, &progress_); });
}
void BulkRename::update()
{
    if (!busy() || !work_.done.load()) return;
    if (!finish_work()) return;
    if (applying_) {
        if (result_ == vault::VaultResult::Ok) {
            completed_ = true;
            close();
        } else {
            error_ = "Rename cancelled, stale, or save failed. No rename applied.";
            preview_ready_ = false;
        }
    } else {
        preview_ready_ = true;
        error_ = std::string(preview_.error.view());
    }
}
bool BulkRename::consume_completed()
{
    return std::exchange(completed_, false);
}
bool BulkRename::handle_event(const SDL_Event& e)
{
    if (!active_) return false;
    if (busy()) {
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE &&
            progress_.total.load() != -1)
            progress_.cancel = true;
        return true;
    }
    if (e.type == SDL_EVENT_KEY_DOWN) {
        const auto key = e.key.key;
        if (key == SDLK_ESCAPE) {
            close();
            return true;
        }
        if (!exclusive_) return true;
        if (key == SDLK_TAB) {
            second_field_ = !second_field_;
            return true;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (preview_ready_ && preview_.valid())
                apply();
            else
                preview();
            return true;
        }
        if (key == SDLK_PAGEUP) {
            first_row_ = first_row_ > 0 ? first_row_ - 1 : 0;
            return true;
        }
        if (key == SDLK_PAGEDOWN) {
            if (first_row_ + 1 < preview_.rows.size()) ++first_row_;
            return true;
        }
        if (key == SDLK_F7) {
            details_ = !details_;
            return true;
        }
        bool changed = true;
        switch (key) {
        case SDLK_F3:
            options_.preserve_extensions = !options_.preserve_extensions;
            break;
        case SDLK_F4:
            options_.recursive = !options_.recursive;
            break;
        case SDLK_F5:
            options_.case_sensitive = !options_.case_sensitive;
            break;
        case SDLK_F6:
            options_.all_matches = !options_.all_matches;
            break;
        default:
            changed = false;
            break;
        }
        if (changed) {
            preview_ready_ = false;
            return true;
        }
    }
    ITextInput& field = second_field_ ? static_cast<ITextInput&>(replacement_) : pattern_;
    if (handle_text_input_event(field, e)) preview_ready_ = false;
    return true;
}
void BulkRename::render(gfx::Renderer& r, gfx::FontAtlas& font, float w, float h)
{
    if (!active_) return;
    if (busy()) {
        const int total = progress_.total.load();
        const int done = progress_.done.load();
        const auto counts =
            total > 0 ? std::format("{} / {} nodes", done, total) : std::string{"Preparing..."};
        draw_op_progress(
            r, font, w, h,
            {.title = applying_ ? "Validating and saving rename" : "Preparing regex preview",
             .count_line = total == -1 ? "Saving atomically..." : std::string_view{counts},
             .hint = total == -1 ? "Commit in progress; cancellation unavailable" : "Esc to cancel",
             .done = done,
             .total = std::max(total, 0)});
        return;
    }
    using namespace gfx::theme;
    const float pitch = line_pitch(font.pixel_height());
    const float mw = w * 0.92f, mx = (w - mw) / 2, my = h * 0.04f;
    r.draw_round_rect({mx, my, mw, h * 0.92f}, RADIUS, SURFACE);
    const float x = mx + 16, width = mw - 32;
    float y = my + 12;
    auto line = [&](std::string_view text) {
        r.draw_text(font, x, y, fit_text(font, text, width), TEXT);
        y += pitch;
    };
    if (details_ && preview_ready_ && first_row_ < preview_.rows.size()) {
        line(std::format("Target {} / {}  [PgUp/PgDn] Target  [F7] Back", first_row_ + 1,
                         preview_.rows.size()));
        auto wrapped = [&](std::string_view value) {
            while (!value.empty()) {
                size_t end = 0;
                const auto bytes =
                    std::span(reinterpret_cast<const uint8_t*>(value.data()), value.size());
                while (end < value.size()) {
                    const size_t next = utf8_next_boundary(bytes, end);
                    if (end != 0 && static_cast<float>(font.measure(value.substr(0, next))) > width)
                        break;
                    end = next;
                }
                line(value.substr(0, end));
                value.remove_prefix(end);
            }
        };
        const auto& row = preview_.rows[first_row_];
        line("Old name:");
        wrapped(row.before.view());
        line("New name:");
        wrapped(row.after.view());
        return;
    }
    line("Bulk rename: PCRE2 UTF-8; $1 / ${name} captures, $$ literal dollar");
    line(std::format("[F3] Preserve extensions: {}  [F4] Include descendants: {}",
                     options_.preserve_extensions, options_.recursive));
    line(std::format("[F5] Case sensitive: {}  [F6] All matches: {}", options_.case_sensitive,
                     options_.all_matches));
    line("Pattern:");
    draw_edit_field(r, font, {x, y, width, pitch + 8}, pattern_, pattern_chrome_, !second_field_);
    y += pitch + 12;
    line("Replacement:");
    draw_edit_field(r, font, {x, y, width, pitch + 8}, replacement_, replacement_chrome_,
                    second_field_);
    y += pitch + 12;
    line(preview_ready_ && preview_.valid()
             ? "[Enter] Apply this preview   [Tab] Field   [Esc] Cancel"
             : "[Enter] Preview   [Tab] Field   [Esc] Cancel");
    if (!error_.empty()) line(error_);
    if (!preview_ready_) return;
    line(std::format("{} targets (including unchanged). [PgUp/PgDn] Scroll  [F7] Full names",
                     preview_.rows.size()));
    static constexpr const char* labels[]{"change", "unchanged", "unmatched",
                                          "invalid / regex limit", "collision"};
    for (size_t i = first_row_;
         i < preview_.rows.size() && i < first_row_ + 5 && y + 2 * pitch < h * 0.94f; ++i) {
        const auto& row = preview_.rows[i];
        line(std::format("{}: {}", labels[static_cast<size_t>(row.state)], row.before.view()));
        line(std::format("  to: {}", row.after.view()));
    }
}
}  // namespace ui
