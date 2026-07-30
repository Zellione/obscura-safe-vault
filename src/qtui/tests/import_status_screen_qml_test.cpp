#include <QtTest>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

class ImportStatusScreenQmlTest : public QObject {
    Q_OBJECT

private slots:
    void testImportStatusScreenQmlFileExists()
    {
        // Verify ImportStatusScreen.qml file exists at expected location
        QFileInfo qmlFile(QString::fromUtf8(QTUI_QML_DIR) + "/ImportStatusScreen.qml");
        QVERIFY2(qmlFile.exists(),
                QString("ImportStatusScreen.qml not found at %1").arg(QTUI_QML_DIR).toStdString().c_str());
        QVERIFY(qmlFile.isFile());
    }

    void testImportStatusScreenHasRequiredStructure()
    {
        // Read the QML file and verify it has basic structure
        QString qmlPath = QString::fromUtf8(QTUI_QML_DIR) + "/ImportStatusScreen.qml";
        QFile qmlFile(qmlPath);
        QVERIFY(qmlFile.open(QIODevice::ReadOnly | QIODevice::Text));

        QTextStream in(&qmlFile);
        QString content = in.readAll();
        qmlFile.close();

        // Verify key components are present
        QVERIFY(content.contains("Rectangle"));        // Root element
        QVERIFY(content.contains("signal back()"));     // back signal
        QVERIFY(content.contains("importController"));  // required property
        QVERIFY(content.contains("themePalette"));      // required property
        QVERIFY(content.contains("Keys.onEscapePressed")); // keyboard handling
    }

    void testImportStatusScreenHasKeyboardHandling()
    {
        // Verify keyboard handlers exist
        QString qmlPath = QString::fromUtf8(QTUI_QML_DIR) + "/ImportStatusScreen.qml";
        QFile qmlFile(qmlPath);
        QVERIFY(qmlFile.open(QIODevice::ReadOnly | QIODevice::Text));

        QTextStream in(&qmlFile);
        QString content = in.readAll();
        qmlFile.close();

        // Per brief: Shift+I/Del/C/Esc handling
        QVERIFY(content.contains("onEscapePressed"));   // Esc to close
        QVERIFY(content.contains("onDeletePressed"));   // Del to cancel
        QVERIFY(content.contains("Key_C"));             // C to clear finished
    }
};

QTEST_MAIN(ImportStatusScreenQmlTest)
#include "import_status_screen_qml_test.moc"

