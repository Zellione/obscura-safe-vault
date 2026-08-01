#include <cstdio>
#include <QGuiApplication>
#include "qml_test_util.h"
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>
#include <QObject>
#include <QUrl>
#include <QtQml/qqml.h>

#include "../theme_palette.h"
#include "../favorites_controller.h"
#include "../gallery_model.h"
#include "../viewer_controller.h"
#include "../selection_controller.h"
#include "../secure_image_item.h"

// Test 1: FavoritesScreen.qml loads without QML compile errors
static bool test_favorites_screen_qml_loads()
{
    printf("Test 1: FavoritesScreen.qml loads without errors...\n");

    QQmlEngine engine;
    qmlRegisterType<SecureImageItem>("Osv", 1, 0, "SecureImageItem");
    QUrl qmlPath = QUrl::fromLocalFile(QTUI_QML_DIR "/FavoritesScreen.qml");
    QQmlComponent component(&engine, qmlPath);

    if (!osvqt_test::expect_qml_loads(component, "FavoritesScreen.qml")) {
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
    qmlRegisterType<SecureImageItem>("Osv", 1, 0, "SecureImageItem");

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

    QUrl qmlPath = QUrl::fromLocalFile(QTUI_QML_DIR "/FavoritesScreen.qml");
    QQmlComponent component(&engine, qmlPath);

    if (component.isError()) {
        fprintf(stderr, "FAIL: Component has errors (see Test 1)\n");
        return false;
    }

    // T3.1 W5: the screen reads controllers from context properties directly
    // (required-property shadowing broke self-named bindings in the shell).
    QObject* obj = component.create();
    if (!obj) {
        fprintf(stderr, "FAIL: FavoritesScreen instantiation failed\n");
        osvqt_test::print_component_errors(component, "  Error");
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
