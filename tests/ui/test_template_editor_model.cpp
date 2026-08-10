#include <SDL3/SDL.h>

#include "../test_framework.h"
#include "ui/template_editor.h"

TEST(template_edit_action_keymap)
{
    using enum ui::TemplateEditAction;
    CHECK(ui::template_edit_action(SDLK_A) == AddField);
    CHECK(ui::template_edit_action(SDLK_R) == RenameField);
    CHECK(ui::template_edit_action(SDLK_DELETE) == RemoveField);
    CHECK(ui::template_edit_action(SDLK_ESCAPE) == Back);
    CHECK(ui::template_edit_action(SDLK_Z) == None);
}
