#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QString>
#include <iostream>

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;

    std::cout << "Test 1: AdvancedSearchScreen.qml loads without errors...\n";

    const QString qmlPath = QStringLiteral(QTUI_QML_DIR "/AdvancedSearchScreen.qml");
    const QUrl url = QUrl::fromLocalFile(qmlPath);

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

    std::cout << "PASS\n";

    std::cout << "\n=== Test Summary ===\n";
    std::cout << "Passed: 1\n";
    std::cout << "Failed: 0\n";

    return 0;
}
