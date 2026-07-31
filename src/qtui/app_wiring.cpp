#include "app_wiring.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml/qqml.h>
#include <cstdlib>

#include "platform/theme_pref.h"

#include "secure_text_field.h"
#include "secure_image_item.h"
#include "video_frame_item.h"
#include "unlock_controller.h"
#include "vault_list_model.h"
#include "vault_manager_controller.h"
#include "gallery_model.h"
#include "thumb_cache.h"
#include "viewer_controller.h"
#include "theme_palette.h"
#include "playback_engine.h"
#include "file_op_controller.h"
#include "status_controller.h"
#include "settings_controller.h"
#include "help_model.h"
#include "selection_controller.h"
#include "session_state.h"
#include "anim_controller.h"
#include "animated_image_loader.h"
#include "detail_controller.h"
#include "autolock.h"
#include "gfx/theme.h"

void initThemeFromEnv()
{
    // Phase 1: Load persisted theme from config (like SDL app does)
    auto pref = platform::ThemePref::default_location();
    auto loadedTheme = pref.load();
    gfx::set_theme(loadedTheme);

    // Phase 2: Let OSV_QT_THEME env override (for selftest/testing)
    const char* theme_env = std::getenv("OSV_QT_THEME");
    if (theme_env) {
        int theme_idx = std::atoi(theme_env);
        if (theme_idx >= 0 && theme_idx < gfx::THEME_COUNT) {
            gfx::set_theme(static_cast<gfx::ThemeId>(theme_idx));
        }
    }
}

void registerOsvQmlTypes()
{
    qmlRegisterType<SecureTextField>("Osv", 1, 0, "SecureTextField");
    qmlRegisterType<SecureImageItem>("Osv", 1, 0, "SecureImageItem");
    qmlRegisterType<VideoFrameItem>("Osv", 1, 0, "VideoFrameItem");
    qmlRegisterType<FileOpController>("Osv", 1, 0, "FileOpController");
    // AnimController and AnimatedImageLoader are created programmatically in C++
    // not exposed to QML as creatable types
}

AppContext::AppContext()
    : galleryModel(&unlockController.vault()),
      viewerController(&unlockController.vault(), &galleryModel),
      detailController(&unlockController.vault())
{
    unlockController.setViewerController(&viewerController);
    unlockController.setPlaybackEngine(&playbackEngine);
    galleryModel.setViewerController(&viewerController);
    playbackEngine.setVault(&unlockController.vault());
    playbackEngine.setSessionState(&sessionState);

    // Wire AutoLock
    autoLock.setUnlockController(&unlockController);

    // Wire vault unlock state to settings controller
    QObject::connect(&unlockController, &UnlockController::unlockedChanged,
                     &settingsController, [this]() {
                         settingsController.setVaultUnlocked(unlockController.unlocked());
                     });

    // Wire SelectionController to GalleryModel for name-keyed selection
    selectionController.setNameLookup([this](int row) {
        return galleryModel.nameAt(row);
    });
}

void AppContext::expose(QQmlApplicationEngine& engine)
{
    QQmlContext* ctx = engine.rootContext();
    ctx->setContextProperty("unlockController", &unlockController);
    ctx->setContextProperty("vaultManagerController", &vaultManagerController);
    ctx->setContextProperty("thumbCache", &thumbCache);
    ctx->setContextProperty("galleryModel", &galleryModel);
    ctx->setContextProperty("viewerController", &viewerController);
    ctx->setContextProperty("themePalette", &themePalette);
    ctx->setContextProperty("playbackEngine", &playbackEngine);
    ctx->setContextProperty("vaultListModel", &vaultListModel);
    ctx->setContextProperty("fileOpController", &fileOpController);
    ctx->setContextProperty("statusController", &statusController);
    ctx->setContextProperty("settingsController", &settingsController);
    ctx->setContextProperty("helpModel", &helpModel);
    ctx->setContextProperty("selectionController", &selectionController);
    ctx->setContextProperty("sessionState", &sessionState);
    ctx->setContextProperty("detailController", &detailController);
    ctx->setContextProperty("autoLock", &autoLock);
}
