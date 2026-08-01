#include <cstdio>
#include <QGuiApplication>
#include "qml_test_util.h"
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>
#include <QObject>
#include <QUrl>
#include <QString>
#include <QVariant>
#include <QtQml/qqml.h>

#include "../theme_palette.h"
#include "../status_controller.h"
#include "../unlock_controller.h"
#include "../secure_text_field.h"

// Test 1: CreateVaultDialog.qml loads without QML compile errors
static bool test_create_vault_dialog_qml_loads()
{
    printf("Test 1: CreateVaultDialog.qml loads without errors...\n");

    QQmlEngine engine;
    // CreateVaultDialog.qml does `import Osv 1.0` for SecureTextField.
    qmlRegisterType<SecureTextField>("Osv", 1, 0, "SecureTextField");
    QUrl qmlPath = QUrl::fromLocalFile(QTUI_QML_DIR "/CreateVaultDialog.qml");
    QQmlComponent component(&engine, qmlPath);

    if (!osvqt_test::expect_qml_loads(component, "CreateVaultDialog.qml")) {
        return false;
    }

    printf("PASS\n");
    return true;
}

// Test 2: the default vault path is a usable absolute file: URL, not "/tmp"
//
// Regression test for two coupled defects in the create-vault path:
//   - the fallback location was a hardcoded "/tmp/vault.osv", which does not
//     exist on Windows (and is a poor place for a vault on any platform), and
//   - the path field's initial text was the bare label "vault.osv", which
//     attemptCreate() passed straight through as a URL. A schemeless relative
//     URL yields an empty QUrl::toLocalFile(), so createVault() rejected it as
//     "Invalid path" — i.e. hitting Create without first opening the file
//     dialog failed on every platform.
// Both now come from UnlockController::defaultVaultPath().
static bool test_default_vault_path_is_absolute_local_file()
{
    printf("Test 2: defaultVaultPath is an absolute local file URL...\n");

    UnlockController unlockController;
    const QUrl def = unlockController.defaultVaultPath();

    if (!def.isValid() || !def.isLocalFile()) {
        fprintf(stderr, "FAIL: defaultVaultPath is not a valid local file URL: %s\n",
                def.toString().toStdString().c_str());
        return false;
    }
    // The bug was a relative URL surviving into createVault(); an empty
    // toLocalFile() is exactly the symptom that produced "Invalid path".
    const QString local = def.toLocalFile();
    if (local.isEmpty()) {
        fprintf(stderr, "FAIL: defaultVaultPath has an empty local file path\n");
        return false;
    }
    if (!local.endsWith(QStringLiteral("vault.osv"))) {
        fprintf(stderr, "FAIL: defaultVaultPath does not name vault.osv: %s\n",
                local.toStdString().c_str());
        return false;
    }
    if (local.startsWith(QStringLiteral("/tmp/"))) {
        fprintf(stderr, "FAIL: defaultVaultPath is back in /tmp: %s\n",
                local.toStdString().c_str());
        return false;
    }

    printf("PASS (%s)\n", local.toStdString().c_str());
    return true;
}

// Test 3: instantiates, and the path field is seeded with that default
static bool test_create_vault_dialog_seeds_path_field()
{
    printf("Test 3: CreateVaultDialog seeds its path field with the default...\n");

    QQmlEngine engine;
    qmlRegisterType<SecureTextField>("Osv", 1, 0, "SecureTextField");

    ThemePalette themePalette;
    StatusController statusController;
    UnlockController unlockController;

    QQmlContext* ctx = engine.rootContext();
    ctx->setContextProperty("themePalette", &themePalette);
    ctx->setContextProperty("statusController", &statusController);
    ctx->setContextProperty("unlockController", &unlockController);

    QUrl qmlPath = QUrl::fromLocalFile(QTUI_QML_DIR "/CreateVaultDialog.qml");
    QQmlComponent component(&engine, qmlPath);

    if (component.isError()) {
        fprintf(stderr, "FAIL: Component has errors (see Test 1)\n");
        return false;
    }

    QObject* obj = osvqt_test::instantiate_qml_component(component, "CreateVaultDialog");
    if (obj == nullptr) {
        return false;
    }

    // The field is bound to unlockController.defaultVaultPath, so the binding
    // must have resolved at component creation — not only when Create is clicked.
    QObject* field = obj->findChild<QObject*>(QStringLiteral("vaultPathField"));
    if (field == nullptr) {
        fprintf(stderr, "FAIL: could not find vaultPathField\n");
        delete obj;
        return false;
    }

    const QString text = field->property("text").toString();
    const QString expected = unlockController.defaultVaultPath().toString();
    if (text != expected) {
        fprintf(stderr, "FAIL: path field is \"%s\", expected \"%s\"\n",
                text.toStdString().c_str(), expected.toStdString().c_str());
        delete obj;
        return false;
    }

    delete obj;
    printf("PASS\n");
    return true;
}

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);

    int passed = 0;
    int failed = 0;

    if (test_create_vault_dialog_qml_loads()) { ++passed; } else { ++failed; }
    if (test_default_vault_path_is_absolute_local_file()) { ++passed; } else { ++failed; }
    if (test_create_vault_dialog_seeds_path_field()) { ++passed; } else { ++failed; }

    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);
    return failed == 0 ? 0 : 1;
}
