#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>
#include <QtQml/qqml.h>

#include "secure_text_field.h"

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    qmlRegisterType<SecureTextField>("Osv", 1, 0, "SecureTextField");
    QQmlApplicationEngine engine;
    engine.load(QUrl::fromLocalFile(QStringLiteral(QTUI_QML_DIR "/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    return QGuiApplication::exec();
}
