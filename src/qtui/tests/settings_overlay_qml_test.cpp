#include <cstdio>
#include <QGuiApplication>
#include <QObject>
#include <QUrl>

#include "qml_test_util.h"

// Test 1: SettingsOverlay.qml loads without QML compile errors
static bool test_settings_overlay_qml_loads()
{
    printf("Test 1: SettingsOverlay.qml loads without errors...\n");

    QQmlEngine engine;
    QUrl qmlPath = QUrl::fromLocalFile(QTUI_QML_DIR "/SettingsOverlay.qml");
    QQmlComponent component(&engine, qmlPath);

    if (!osvqt_test::expect_qml_loads(component, "SettingsOverlay.qml")) {
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 2: SettingsOverlay instantiates without runtime errors
static bool test_settings_overlay_instantiates()
{
    printf("Test 2: SettingsOverlay instantiates without errors...\n");

    QQmlEngine engine;
    QUrl qmlPath = QUrl::fromLocalFile(QTUI_QML_DIR "/SettingsOverlay.qml");
    QQmlComponent component(&engine, qmlPath);

    if (!osvqt_test::expect_qml_loads(component, "SettingsOverlay")) {
        return false;
    }

    QObject* obj = osvqt_test::instantiate_qml_component(component, "SettingsOverlay");
    if (!obj) {
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

    if (test_settings_overlay_qml_loads()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_settings_overlay_instantiates()) {
        ++passed;
    } else {
        ++failed;
    }

    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
