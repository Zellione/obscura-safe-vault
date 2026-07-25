// Phase 53: the multi-volume confirm dialog's decision logic.
//
// The load-bearing rule: a set with a gap CANNOT be confirmed. Importing an
// incomplete set produces a corrupt gallery rather than a partial one, so Enter
// must do nothing — the dialog stays up with the warning visible rather than
// letting the user click past it.

#include "test_framework.h"

#include <SDL3/SDL.h>

#include "ui/volume_set_dialog.h"

namespace {

ui::VolumeSetSummary complete_set()
{
    ui::VolumeSetSummary s;
    s.heading      = "Import 3 volumes as one archive?";
    s.volume_lines = {"a.7z.001", "a.7z.002", "a.7z.003"};
    s.can_import   = true;
    return s;
}

ui::VolumeSetSummary gapped_set()
{
    ui::VolumeSetSummary s;
    s.heading      = "Import 2 volumes as one archive?";
    s.volume_lines = {"a.7z.001", "a.7z.003"};
    s.warning      = "Missing volume: 2 — cannot import an incomplete set.";
    s.can_import   = false;
    return s;
}

} // namespace

TEST(volume_dialog_starts_inactive)
{
    const ui::VolumeSetDialog d;
    CHECK_FALSE(d.active());
}

TEST(volume_dialog_opens_and_keeps_its_summary)
{
    ui::VolumeSetDialog d;
    d.open(complete_set());
    CHECK(d.active());
    REQUIRE(d.summary().volume_lines.size() == 3);
    CHECK_EQ(d.summary().volume_lines[0], std::string("a.7z.001"));
}

TEST(volume_dialog_enter_confirms_a_complete_set)
{
    ui::VolumeSetDialog d;
    d.open(complete_set());
    CHECK(d.handle_key(SDLK_RETURN) == ui::VolumeSetDialog::Result::Confirmed);
    CHECK_FALSE(d.active());   // decisive key closes it
}

TEST(volume_dialog_escape_cancels)
{
    ui::VolumeSetDialog d;
    d.open(complete_set());
    CHECK(d.handle_key(SDLK_ESCAPE) == ui::VolumeSetDialog::Result::Cancelled);
    CHECK_FALSE(d.active());
}

TEST(volume_dialog_refuses_to_confirm_a_gapped_set)
{
    // THE rule. Enter is not a way past a missing volume.
    ui::VolumeSetDialog d;
    d.open(gapped_set());
    CHECK(d.handle_key(SDLK_RETURN) == ui::VolumeSetDialog::Result::Pending);
    CHECK(d.active());   // stays up so the warning remains visible
}

TEST(volume_dialog_gapped_set_can_still_be_cancelled)
{
    ui::VolumeSetDialog d;
    d.open(gapped_set());
    CHECK(d.handle_key(SDLK_ESCAPE) == ui::VolumeSetDialog::Result::Cancelled);
    CHECK_FALSE(d.active());
}

TEST(volume_dialog_ignores_unrelated_keys)
{
    ui::VolumeSetDialog d;
    d.open(complete_set());
    CHECK(d.handle_key(SDLK_TAB) == ui::VolumeSetDialog::Result::Pending);
    CHECK(d.active());
}

TEST(volume_dialog_ignores_keys_while_inactive)
{
    // A stray Enter after the dialog closed must not re-confirm an import.
    ui::VolumeSetDialog d;
    CHECK(d.handle_key(SDLK_RETURN) == ui::VolumeSetDialog::Result::Pending);
    CHECK_FALSE(d.active());
}

TEST(volume_dialog_y_and_n_mirror_enter_and_escape)
{
    ui::VolumeSetDialog d;
    d.open(complete_set());
    CHECK(d.handle_key(SDLK_Y) == ui::VolumeSetDialog::Result::Confirmed);

    d.open(complete_set());
    CHECK(d.handle_key(SDLK_N) == ui::VolumeSetDialog::Result::Cancelled);
}
