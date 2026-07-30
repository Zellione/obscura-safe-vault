#ifndef APP_WIRING_H
#define APP_WIRING_H

#include "unlock_controller.h"
#include "vault_list_model.h"
#include "gallery_model.h"
#include "thumb_cache.h"
#include "viewer_controller.h"
#include "theme_palette.h"
#include "playback_engine.h"
#include "file_op_controller.h"
#include "status_controller.h"

class QQmlApplicationEngine;

// Initialize theme from environment variable if specified (OSV_QT_THEME=0..3)
void initThemeFromEnv();

// Register OSV QML types (SecureTextField, SecureImageItem, VideoFrameItem)
void registerOsvQmlTypes();

// The app's controller/model object graph and its QML context wiring, shared
// by the normal run path and the selftest harness.
struct AppContext {
    UnlockController unlockController;
    ThumbCache thumbCache;
    GalleryModel galleryModel;
    ViewerController viewerController;
    ThemePalette themePalette;
    PlaybackEngine playbackEngine;
    VaultListModel vaultListModel;
    FileOpController fileOpController;
    StatusController statusController;

    AppContext();

    void expose(QQmlApplicationEngine& engine);
};

#endif  // APP_WIRING_H
