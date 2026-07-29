#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.load(QUrl::fromLocalFile(QStringLiteral(QTUI_QML_DIR "/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    return QGuiApplication::exec();
}
