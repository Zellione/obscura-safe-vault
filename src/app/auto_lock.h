#pragma once

#include "app/idle_timer.h"

namespace app {

// Pure decision for App::maybe_auto_lock (Phase 33): should this tick lock the
// active vault? "Nothing active", a screen doing background work that must not
// be interrupted (Screen::blocks_idle_lock — e.g. an import writing the vault),
// the session's "keep unlocked" toggle, a busy import queue (Phase 50), and a
// running vault-upgrade MigrationJob (Phase 79 — the job owns the vault
// exclusively; locking it mid-flight tore the vault down under the coordinator
// and wedged the progress modal) all suppress the timer and reset it, so a
// later re-enable starts counting from zero instead of a stale elapsed value.
// Pure / SDL-free so the branches are unit-testable without an App.
[[nodiscard]] inline bool should_auto_lock(bool has_active, bool blocks_idle_lock,
                                           bool keep_unlocked, bool import_busy,
                                           bool migration_active,
                                           IdleTimer& timer, double dt) noexcept
{
    if (!has_active || blocks_idle_lock || keep_unlocked || import_busy || migration_active) {
        timer.reset();
        return false;
    }
    return timer.tick(dt);
}

} // namespace app
