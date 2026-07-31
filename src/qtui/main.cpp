#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>
#include <cstring>
#include <cstdio>

#include "app_wiring.h"
#include "qml_dir.h"
#include "selftest.h"

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    // Check for selftest flags
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--selftest-render") == 0) {
            return runSelftestRender();
        }
        if (strcmp(argv[i], "--selftest-image") == 0 && i + 1 < argc) {
            QString vaultPath = QString::fromUtf8(argv[i + 1]);
            return runSelftest(vaultPath);
        }
    }

    // Normal flow
    initThemeFromEnv();
    registerOsvQmlTypes();

    QQmlApplicationEngine engine;

    const QString qmlDir = resolveQmlDir();

    // Add QML directory to import path so it can find UnlockScreen.qml and GalleryScreen.qml
    engine.addImportPath(qmlDir);

    // Connect warnings to stderr for debugging
    QObject::connect(&engine, &QQmlApplicationEngine::warnings,
                     [](const QList<QQmlError>& warnings) {
        for (const auto& w : warnings) {
            fprintf(stderr, "QML warning: %s:%d %s\n",
                    w.url().toLocalFile().toStdString().c_str(),
                    w.line(),
                    w.description().toStdString().c_str());
        }
    });

    AppContext appCtx;
    appCtx.expose(engine);

    const QString qmlPath = qmlDir + QStringLiteral("/Main.qml");
    engine.load(QUrl::fromLocalFile(qmlPath));
    if (engine.rootObjects().isEmpty()) {
        fprintf(stderr, "QML load failed: %s\n", qmlPath.toStdString().c_str());
        return 1;
    }

    // Connect unlock signal to update models
    QObject::connect(&appCtx.unlockController, &UnlockController::unlockedChanged, [&appCtx]() {
        if (appCtx.unlockController.unlocked()) {
            appCtx.thumbCache.setVault(&appCtx.unlockController.vault());
            appCtx.galleryModel.refresh();
        } else {
            appCtx.thumbCache.clearAll();
        }
    });

    return QGuiApplication::exec();
}
