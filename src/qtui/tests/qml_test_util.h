#pragma once

// Shared helpers for Qt QML component tests: load, error reporting, instantiation.
// Mirrors the conventions of test_vault_util.h. Test QML components only —
// never used outside test binaries.

#include <cstdio>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QObject>
#include <QUrl>

namespace osvqt_test {

// Print all errors from a QQmlComponent to stderr with given prefix label
inline void print_component_errors(const QQmlComponent& component, const char* label = "QML error")
{
    for (const auto& error : component.errors()) {
        fprintf(stderr, "%s at line %d: %s\n",
                label,
                error.line(),
                error.description().toStdString().c_str());
    }
}

// Convenience: load QML file by path string (QTUI_QML_DIR macro expected in calling context).
// Caller provides the QQmlComponent and QQmlEngine (non-copyable types).
// Returns true if load succeeded (no errors); false and prints errors if load failed.
inline bool expect_qml_loads(const QQmlComponent& component, const char* componentName)
{
    if (component.isError()) {
        fprintf(stderr, "FAIL: %s has QML errors:\n", componentName);
        print_component_errors(component, "  Line");
        return false;
    }
    return true;
}

// Instantiate a component (must have been loaded and verified to have no errors).
// Returns the created QObject* (caller owns it and must delete), or nullptr on failure.
// Prints error details to stderr on failure.
inline QObject* instantiate_qml_component(QQmlComponent& component, const char* componentName)
{
    QObject* obj = component.create();
    if (!obj) {
        fprintf(stderr, "FAIL: %s instantiation failed\n", componentName);
        if (component.isError()) {
            print_component_errors(component, "  Error");
        }
        return nullptr;
    }
    return obj;
}

// Complete load+instantiate smoke test for components needing no context
// properties. Returns a process exit code: 0 on pass, 1 on fail.
inline int run_component_smoke_test(const QUrl& url, const char* componentName)
{
    QQmlEngine engine;
    QQmlComponent component(&engine, url);

    printf("Test 1: %s loads without errors...\n", componentName);
    if (!expect_qml_loads(component, componentName)) return 1;
    printf("PASS\n");

    printf("Test 2: %s instantiates without errors...\n", componentName);
    QObject* obj = instantiate_qml_component(component, componentName);
    if (!obj) return 1;
    delete obj;
    printf("PASS\n");
    return 0;
}

}  // namespace osvqt_test
