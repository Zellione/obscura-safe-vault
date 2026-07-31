#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QString>
#include <iostream>

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;

    // Load TagOverviewScreen.qml from filesystem using QTUI_QML_DIR (set by CMake)
    const QString qmlPath = QStringLiteral(QTUI_QML_DIR "/TagOverviewScreen.qml");
    const QUrl url = QUrl::fromLocalFile(qmlPath);

    std::cout << "Test 1: TagOverviewScreen.qml loads without errors...\n";

    QQmlComponent component(&engine, url);
    if (component.isError()) {
        std::cerr << "FAIL: " << qmlPath.toStdString() << " has QML errors:\n";
        for (const auto& error : component.errors()) {
            std::cerr << "  Line " << error.line() << ": " << error.description().toStdString() << "\n";
        }
        std::cout << "FAIL\n";
        return 1;
    }

    std::cout << "PASS\n";

    std::cout << "Test 2: TagOverviewScreen instantiates without errors...\n";

    // Create an instance
    QObject* obj = component.create();
    if (!obj) {
        std::cerr << "FAIL: Failed to instantiate TagOverviewScreen\n";
        return 1;
    }

    delete obj;
    std::cout << "PASS\n";

    std::cout << "\n=== Test Summary ===\n";
    std::cout << "Passed: 2\n";
    std::cout << "Failed: 0\n";

    return 0;
}
