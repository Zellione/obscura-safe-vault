#include <cstdio>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QObject>
#include <QUrl>

// Test 1: TagAutocompletePopup.qml loads without QML compile errors
static bool test_tag_autocomplete_popup_qml_loads()
{
    printf("Test 1: TagAutocompletePopup.qml loads without errors...\n");

    QQmlEngine engine;
    QUrl qmlPath("file://" QTUI_QML_DIR "/TagAutocompletePopup.qml");
    QQmlComponent component(&engine, qmlPath);

    if (component.isError()) {
        fprintf(stderr, "FAIL: TagAutocompletePopup.qml has QML errors:\n");
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

// Test 2: TagAutocompletePopup instantiates without runtime errors
static bool test_tag_autocomplete_popup_instantiates()
{
    printf("Test 2: TagAutocompletePopup instantiates without errors...\n");

    QQmlEngine engine;
    QUrl qmlPath("file://" QTUI_QML_DIR "/TagAutocompletePopup.qml");
    QQmlComponent component(&engine, qmlPath);

    if (component.isError()) {
        fprintf(stderr, "FAIL: Component has errors (see Test 1)\n");
        return false;
    }

    QObject* obj = component.create();
    if (!obj) {
        fprintf(stderr, "FAIL: TagAutocompletePopup instantiation failed\n");
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

    if (test_tag_autocomplete_popup_qml_loads()) {
        ++passed;
    } else {
        ++failed;
    }

    if (test_tag_autocomplete_popup_instantiates()) {
        ++passed;
    } else {
        ++failed;
    }

    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
