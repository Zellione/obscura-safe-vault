#include "test_framework.h"
#include "gfx/text.h"
#include "gfx/texture_cache.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "platform/folder_dialog.h"
#include "platform/vault_registry.h"
#include "ui/gallery_grid.h"
#include "ui/zip_test_helpers.h"

namespace ui {
struct GalleryRenameTestPeer {
    static void list(GalleryGrid& grid, float scroll) {
        grid.view_ = GalleryView::List;
        grid.scroll_ = scroll;
    }
    static std::string target(const GalleryGrid& grid) {
        return std::string(grid.rename_.new_name());
    }
};
}

TEST(gallery_rename_hover_scrolled_row_and_keyboard_focus)
{
    const auto dir = ziptest::fresh_dir("rename_hover");
    {
        vault::Vault vault;
        REQUIRE(vaulttest::make_directory_vault(vault, dir / "vault.osv"));
        REQUIRE(vault.create_gallery("Alpha") == vault::VaultResult::Ok);
        REQUIRE(vault.create_gallery("Beta") == vault::VaultResult::Ok);
        gfx::Window window;
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        REQUIRE(window.init());
        SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
        gfx::FontAtlas font;
        gfx::TextureCache cache(window.sdl_renderer());
        platform::FileDialog files;
        platform::FolderDialog folders;
        platform::VaultRegistry registry(dir / "vaults.list");
        ui::GallerySessionState session;
        ui::ImportQueue queue;
        ui::GalleryGrid grid({window, font, vault, cache}, {files, folders},
                            {registry, (dir / "vault.osv").string(), nullptr}, session, queue);
        grid.on_enter();
        ui::GalleryRenameTestPeer::list(grid, 44);
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.x = 100;
        motion.motion.y = 200;
        grid.handle_event(motion);
        auto press = [&](SDL_Keycode key) {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.key = key;
            grid.handle_event(event);
        };
        press(SDLK_R);
        CHECK(ui::GalleryRenameTestPeer::target(grid) == "Beta");
        // Pointer motion while editing must not change the rename target.
        motion.motion.y = 170;
        grid.handle_event(motion);
        CHECK(ui::GalleryRenameTestPeer::target(grid) == "Beta");
        press(SDLK_ESCAPE);
        press(SDLK_HOME);
        press(SDLK_R);
        CHECK(ui::GalleryRenameTestPeer::target(grid) == "Alpha");
        press(SDLK_ESCAPE);
    }
    std::filesystem::remove_all(dir);
}
