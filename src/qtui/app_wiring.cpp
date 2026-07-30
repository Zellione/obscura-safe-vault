#include "app_wiring.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml/qqml.h>
#include <cstdlib>

#include "secure_text_field.h"
#include "secure_image_item.h"
#include "video_frame_item.h"
#include "unlock_controller.h"
#include "vault_list_model.h"
#include "gallery_model.h"
#include "thumb_cache.h"
#include "viewer_controller.h"
#include "theme_palette.h"
#include "playback_engine.h"
#include "file_op_controller.h"
#include "status_controller.h"
#include "gfx/theme.h"

void initThemeFromEnv()
{
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
}

AppContext::AppContext()
    : galleryModel(&unlockController.vault()),
      viewerController(&unlockController.vault(), &galleryModel)
{
    unlockController.setViewerController(&viewerController);
    unlockController.setPlaybackEngine(&playbackEngine);
    galleryModel.setViewerController(&viewerController);
    playbackEngine.setVault(&unlockController.vault());
}

void AppContext::expose(QQmlApplicationEngine& engine)
{
    QQmlContext* ctx = engine.rootContext();
    ctx->setContextProperty("unlockController", &unlockController);
    ctx->setContextProperty("thumbCache", &thumbCache);
    ctx->setContextProperty("galleryModel", &galleryModel);
    ctx->setContextProperty("viewerController", &viewerController);
    ctx->setContextProperty("themePalette", &themePalette);
    ctx->setContextProperty("playbackEngine", &playbackEngine);
    ctx->setContextProperty("vaultListModel", &vaultListModel);
    ctx->setContextProperty("fileOpController", &fileOpController);
    ctx->setContextProperty("statusController", &statusController);
}
