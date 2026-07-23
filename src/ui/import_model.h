#pragma once

#include <SDL3/SDL_keycode.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

enum class ImportTaskKind  { Files, Zip, Cbz, Archive, ArchiveCbz };
enum class ImportTaskState { Queued, Running, Done, Failed, Cancelled };

// UI-facing snapshot row. `display_name`: the archive filename / "N files".
struct ImportTaskInfo {
    uint64_t        id = 0;
    ImportTaskKind  kind = ImportTaskKind::Files;
    std::string     display_name;
    std::string     dest_gallery;      // "" = root
    ImportTaskState state = ImportTaskState::Queued;
    int             done = 0, total = 0;      // live progress while Running
    int             imported = 0, skipped = 0;
    std::string     error;                    // set for Failed

    bool operator==(const ImportTaskInfo&) const = default;
};

[[nodiscard]] constexpr bool import_task_finished(ImportTaskState s) noexcept
{ return s == ImportTaskState::Done || s == ImportTaskState::Failed ||
         s == ImportTaskState::Cancelled; }

// Reorder a QUEUED task by delta within the queued span of `tasks` (Running
// and finished rows never move). Returns true if anything moved.
[[nodiscard]] bool reorder_import_task(std::vector<ImportTaskInfo>& tasks,
                                       uint64_t id, int delta);

// Drop finished rows. Returns removed count.
int clear_finished_imports(std::vector<ImportTaskInfo>& tasks);

// One-line footer summary; empty when there is nothing running or queued:
//   "Importing <name> 128/450 · 2 queued"   (queued suffix only when > 0)
//   "Import failed: <error>"                (when lane_failed and lane_error non-empty)
//   "Import failed"                         (when lane_failed but lane_error empty)
[[nodiscard]] std::string footer_import_summary(const std::vector<ImportTaskInfo>& tasks,
                                                bool lane_failed,
                                                std::string_view lane_error = {});

// Batched-commit policy (spec: every 32 attached files or 2 s, whichever
// first, and only when at least one file is uncommitted).
struct BatchCommitPolicy {
    static constexpr int    FILES_PER_COMMIT = 32;
    static constexpr double SECS_PER_COMMIT  = 2.0;
    int    files_since_commit = 0;
    double secs_since_commit  = 0.0;
    void   note_attached(int n) noexcept { files_since_commit += n; }
    void   tick(double dt) noexcept { if (files_since_commit > 0) secs_since_commit += dt; }
    [[nodiscard]] bool due() const noexcept
    { return files_since_commit >= FILES_PER_COMMIT ||
             (files_since_commit > 0 && secs_since_commit >= SECS_PER_COMMIT); }
    void   reset() noexcept { files_since_commit = 0; secs_since_commit = 0.0; }
};

// Confirm-dialog decision for a lock-ish action while imports are pending.
enum class LockConfirmKey { Confirm, Cancel, Other };
[[nodiscard]] constexpr LockConfirmKey classify_lock_confirm_key(SDL_Keycode k) noexcept
{
    using enum LockConfirmKey;
    switch (k) {
        case SDLK_Y:                 // confirming requires a deliberate, distinct key
            return Confirm;
        case SDLK_RETURN:            // Enter triggers the default action: Cancel
        case SDLK_KP_ENTER:
        case SDLK_ESCAPE:
        case SDLK_N:
            return Cancel;
        default:
            return Other;
    }
}
// => "3 imports pending — finish current file, discard the rest, and lock?"
//    (singular "1 import pending — …")
[[nodiscard]] std::string import_lock_confirm_text(int pending_tasks);

} // namespace ui
