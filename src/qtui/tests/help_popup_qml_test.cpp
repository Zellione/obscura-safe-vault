#include <cstdio>
#include <QGuiApplication>
#include <QObject>
#include <QUrl>

#include "qml_test_util.h"

// Test 1: HelpPopup.qml loads without QML compile errors
static bool test_help_popup_qml_loads()
{
    printf("Test 1: HelpPopup.qml loads without errors...\n");

    QQmlEngine engine;
    QUrl qmlPath("file://" QTUI_QML_DIR "/HelpPopup.qml");
    QQmlComponent component(&engine, qmlPath);

    if (!osvqt_test::expect_qml_loads(component, "HelpPopup.qml")) {
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 2: HelpPopup instantiates without runtime errors
static bool test_help_popup_instantiates()
{
    printf("Test 2: HelpPopup instantiates without errors...\n");

    QQmlEngine engine;
    QUrl qmlPath("file://" QTUI_QML_DIR "/HelpPopup.qml");
    QQmlComponent component(&engine, qmlPath);

    if (!osvqt_test::expect_qml_loads(component, "HelpPopup")) {
        return false;
    }

    QObject* obj = osvqt_test::instantiate_qml_component(component, "HelpPopup");
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

    if (test_help_popup_qml_loads()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_help_popup_instantiates()) {
        ++passed;
    } else {
        ++failed;
    }

    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
