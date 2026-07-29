#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtQml/qqml.h>

#include "secure_text_field.h"
#include "secure_image_item.h"
#include "unlock_controller.h"

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    qmlRegisterType<SecureTextField>("Osv", 1, 0, "SecureTextField");
    qmlRegisterType<SecureImageItem>("Osv", 1, 0, "SecureImageItem");
    QQmlApplicationEngine engine;

    UnlockController unlockController;
    engine.rootContext()->setContextProperty("unlockController", &unlockController);

    engine.load(QUrl::fromLocalFile(QStringLiteral(QTUI_QML_DIR "/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    return QGuiApplication::exec();
}
