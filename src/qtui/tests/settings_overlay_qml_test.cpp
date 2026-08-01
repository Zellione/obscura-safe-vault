#include <QGuiApplication>

#include "qml_test_util.h"

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    return osvqt_test::run_component_smoke_test(
        QUrl::fromLocalFile(QTUI_QML_DIR "/SettingsOverlay.qml"), "SettingsOverlay");
}
