#include <cstdio>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>
#include <QObject>
#include <QUrl>

#include "../theme_palette.h"
#include "../favorites_controller.h"
#include "../gallery_model.h"
#include "../viewer_controller.h"
#include "../selection_controller.h"

// Test 1: FavoritesScreen.qml loads without QML compile errors
static bool test_favorites_screen_qml_loads()
{
    printf("Test 1: FavoritesScreen.qml loads without errors...\n");

    QQmlEngine engine;
    QUrl qmlPath("file://" QTUI_QML_DIR "/FavoritesScreen.qml");
    QQmlComponent component(&engine, qmlPath);

    if (component.isError()) {
        fprintf(stderr, "FAIL: FavoritesScreen.qml has QML errors:\n");
        for (const auto& error : component.errors()) {
            fprintf(stderr, "  Line %d: %s\n",
                    error.line(),
                    error.description().toStdString().c_str());
        }
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 2: FavoritesScreen instantiates with proper context properties
static bool test_favorites_screen_instantiates()
{
    printf("Test 2: FavoritesScreen instantiates without errors...\n");

    QQmlEngine engine;

    // Create the required objects for context properties
    ThemePalette themePalette;
    FavoritesController favoritesController;
    GalleryModel galleryModel(nullptr);
    ViewerController viewerController(nullptr, &galleryModel);
    SelectionController selectionController;

    // Set context properties
    QQmlContext* ctx = engine.rootContext();
    ctx->setContextProperty("themePalette", &themePalette);
    ctx->setContextProperty("favoritesController", &favoritesController);
    ctx->setContextProperty("galleryModel", &galleryModel);
    ctx->setContextProperty("viewerController", &viewerController);
    ctx->setContextProperty("selectionController", &selectionController);

    QUrl qmlPath("file://" QTUI_QML_DIR "/FavoritesScreen.qml");
    QQmlComponent component(&engine, qmlPath);

    if (component.isError()) {
        fprintf(stderr, "FAIL: Component has errors (see Test 1)\n");
        return false;
    }

    // Create with initial properties
    QVariantMap initialProps;
    initialProps["themePalette"] = QVariant::fromValue(&themePalette);
    initialProps["favoritesController"] = QVariant::fromValue(&favoritesController);
    initialProps["galleryModel"] = QVariant::fromValue(&galleryModel);
    initialProps["viewerController"] = QVariant::fromValue(&viewerController);
    initialProps["selectionController"] = QVariant::fromValue(&selectionController);

    QObject* obj = component.createWithInitialProperties(initialProps);
    if (!obj) {
        fprintf(stderr, "FAIL: FavoritesScreen instantiation failed\n");
        if (component.isError()) {
            for (const auto& error : component.errors()) {
                fprintf(stderr, "  %s\n", error.description().toStdString().c_str());
            }
        }
        return false;
    }

    delete obj;

    printf("PASS\n");
    return true;
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    int passed = 0;
    int failed = 0;

    if (test_favorites_screen_qml_loads()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_favorites_screen_instantiates()) {
        ++passed;
    } else {
        ++failed;
    }

    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
