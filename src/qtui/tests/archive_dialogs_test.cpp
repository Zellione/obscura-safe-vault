#include <QtTest>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

class ArchiveDialogsTest : public QObject {
    Q_OBJECT

private slots:
    void testArchivePasswordDialogQmlExists()
    {
        QFileInfo qmlFile(QString::fromUtf8(QTUI_QML_DIR) + "/ArchivePasswordDialog.qml");
        QVERIFY2(qmlFile.exists(), "ArchivePasswordDialog.qml not found");
    }

    void testVolumeSetDialogQmlExists()
    {
        QFileInfo qmlFile(QString::fromUtf8(QTUI_QML_DIR) + "/VolumeSetDialog.qml");
        QVERIFY2(qmlFile.exists(), "VolumeSetDialog.qml not found");
    }

    void testArchivePasswordDialogHasTextField()
    {
        QString qmlPath = QString::fromUtf8(QTUI_QML_DIR) + "/ArchivePasswordDialog.qml";
        QFile qmlFile(qmlPath);
        QVERIFY(qmlFile.open(QIODevice::ReadOnly | QIODevice::Text));

        QTextStream in(&qmlFile);
        QString content = in.readAll();
        qmlFile.close();

        // Now uses SecureTextField (C++ component) instead of TextField
        QVERIFY(content.contains("SecureTextField"));
        QVERIFY(content.contains("password", Qt::CaseInsensitive));
        QVERIFY(content.contains("importController"));  // password submission hook
    }

    void testVolumeSetDialogHasListView()
    {
        QString qmlPath = QString::fromUtf8(QTUI_QML_DIR) + "/VolumeSetDialog.qml";
        QFile qmlFile(qmlPath);
        QVERIFY(qmlFile.open(QIODevice::ReadOnly | QIODevice::Text));

        QTextStream in(&qmlFile);
        QString content = in.readAll();
        qmlFile.close();

        QVERIFY(content.contains("ListView"));
        QVERIFY(content.contains("volumeSet"));
        QVERIFY(content.contains("allVolumesPresent"));
    }
};

QTEST_MAIN(ArchiveDialogsTest)
#include "archive_dialogs_test.moc"
