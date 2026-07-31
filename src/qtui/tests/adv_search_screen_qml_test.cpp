#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>

#include "qml_test_util.h"

// Load-only check: AdvancedSearchScreen needs context properties to
// instantiate, so instantiation is covered by the app-level QML load gate.
int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    QQmlEngine engine;
    QQmlComponent component(&engine,
                            QUrl::fromLocalFile(QStringLiteral(QTUI_QML_DIR "/AdvancedSearchScreen.qml")));

    printf("Test 1: AdvancedSearchScreen.qml loads without errors...\n");
    if (!osvqt_test::expect_qml_loads(component, "AdvancedSearchScreen.qml")) return 1;
    printf("PASS\n");
    return 0;
}
