// Phase 87: a finished vault migration (MigrationJob) leaves the active screen
// holding stale IndexNode* pointers — the coordinator's compaction replaces
// Vault::root_ with a moved-from copy (src/vault/vault.cpp), freeing the very
// tree the grid was listing. The App must re-list the screen (on_vault_changed)
// and request a redraw BEFORE the next render, or the grid reads a freed node
// name and crashes (the Phase 87 core: SIGSEGV in byte_at() on a torn
// std::string, name._M_p == 0, recovered from the 2026-08-25 crash dump).
//
// apply_migration_refresh() is that refresh. This pins its contract with a
// Screen double: it must notify the screen to re-list AND request a repaint,
// and it must be a clean no-op when there is no active vault or no screen.

#include "test_framework.h"

#include <SDL3/SDL.h>

#include "app/migration_refresh.h"
#include "ui/screen.h"

namespace {
// Records the two effects a refresh must produce: a re-list request
// (on_vault_changed) and a redraw request (mark_dirty).
struct RecordingScreen : ui::Screen {
    int relist_calls = 0;
    void handle_event(const SDL_Event&) override {}
    void render(gfx::Renderer&) override {}
    void on_vault_changed() override { ++relist_calls; }
};
} // namespace

TEST(migration_refresh_relists_and_requests_redraw)
{
    RecordingScreen s;
    (void)s.consume_dirty();   // clear the first-frame default so the redraw assertion is meaningful
    CHECK(app::apply_migration_refresh(/*has_active_vault=*/true, &s));
    CHECK_EQ(s.relist_calls, 1);
    CHECK(s.consume_dirty());   // the refresh must also request a repaint
}

TEST(migration_refresh_is_a_noop_without_an_active_vault)
{
    RecordingScreen s;
    (void)s.consume_dirty();
    CHECK_FALSE(app::apply_migration_refresh(/*has_active_vault=*/false, &s));
    CHECK_EQ(s.relist_calls, 0);
    CHECK_FALSE(s.consume_dirty());
}

TEST(migration_refresh_is_a_noop_without_a_screen)
{
    CHECK_FALSE(app::apply_migration_refresh(/*has_active_vault=*/true, /*screen=*/nullptr));
}
