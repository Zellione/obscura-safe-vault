#pragma once

// Phase 87: the screen refresh that must follow a finished vault migration.
//
// While a MigrationJob is active its coordinator OWNS the vault exclusively and
// mutates the index tree directly — thumbnail/poster regen, then compaction.
// compact() (src/vault/vault.cpp) rebuilds the tree into a copy and publishes it
// with `root_ = std::move(new_root)`, which DESTROYS the tree the grid was
// listing; every cached `const IndexNode*` (GalleryGrid::children_) dangles the
// instant take_outcome() returns. If the App does not re-list the active screen
// before the next frame, the grid reads a freed node's name and crashes — the
// Phase 87 core: SIGSEGV in byte_at() on a torn std::string (name._M_p == 0,
// recovered from the 2026-08-25 crash dump of the 2nd vault).
//
// This is the same refresh the import drain already performs (App::update):
// tell the screen the tree changed and request a redraw, so it re-fetches fresh
// pointers BEFORE the next render. It is pure and vault-dereference-free (takes
// a Screen pointer; the vault is only checked for presence), so the branches are
// unit-testable with a Screen double. App is the sole caller.

#include "ui/screen.h"

namespace app {

[[nodiscard]] inline bool apply_migration_refresh(bool has_active_vault, ui::Screen* screen) noexcept
{
    if (!has_active_vault || !screen) return false;
    screen->on_vault_changed();
    screen->mark_dirty();
    return true;
}

} // namespace app
