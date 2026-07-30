#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QEventLoop>
#include <QTimer>

#include "export_controller.h"
#include "test_vault_util.h"

class ExportControllerTest : public QObject {
    Q_OBJECT

private slots:
    void testExportedBytesMatchOriginals()
    {
        // TDD: real export writes exact decrypted bytes
        QTemporaryDir temp_dir;
        QVERIFY(temp_dir.isValid());

        std::string vault_path = (temp_dir.path() + "/test.osv").toStdString();
        auto vault = osvqt_test::createTestVault(vault_path);

        // Add a test image to the vault
        osvqt_test::addTinyImages(vault, "test", 1);

        // Find the added image in the index
        auto nodes = vault.list("");  // "" = root gallery
        QCOMPARE((int)nodes.size(), 1);
        const auto& image_node = nodes[0];
        QVERIFY(!image_node->is_gallery());

        // Export to temp directory
        QTemporaryDir export_dir;
        QVERIFY(export_dir.isValid());

        ExportController controller;
        QSignalSpy finishedSpy(&controller, &ExportController::finished);

        // Export the image
        QList<quintptr> nodeIds;
        nodeIds.append(reinterpret_cast<quintptr>(image_node));

        controller.setVault(&vault);
        controller.startExport(export_dir.path(), nodeIds);

        // Wait for completion
        if (finishedSpy.isEmpty()) {
            QEventLoop loop;
            QTimer timer;
            timer.setSingleShot(true);
            connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            connect(&controller, &ExportController::finished, &loop, &QEventLoop::quit);
            timer.start(5000);  // 5 second timeout
            loop.exec();
        }

        QVERIFY(!finishedSpy.isEmpty());
        auto args = finishedSpy.takeFirst();
        bool ok = args.at(0).toBool();
        QVERIFY(ok);  // Export must succeed

        // Verify exported file exists and matches original
        QString exported_path = export_dir.path() + "/" +
                               QString::fromStdString(image_node->name);
        QVERIFY(QFile::exists(exported_path));

        QFile exported_file(exported_path);
        QVERIFY(exported_file.open(QIODevice::ReadOnly));
        QByteArray exported_bytes = exported_file.readAll();
        exported_file.close();

        // Compare with original tiny_jpeg
        QCOMPARE((int)exported_bytes.size(), (int)sizeof(osvqt_test::tiny_jpeg));
        QVERIFY(std::memcmp(exported_bytes.data(), osvqt_test::tiny_jpeg,
                           sizeof(osvqt_test::tiny_jpeg)) == 0);
    }

    void testExportContainmentRefusesTraversal()
    {
        // SECURITY TEST: invalid export directory is refused
        // (per CLAUDE.md invariant: path validation)
        QTemporaryDir temp_dir;
        QVERIFY(temp_dir.isValid());

        std::string vault_path = (temp_dir.path() + "/test2.osv").toStdString();
        auto vault = osvqt_test::createTestVault(vault_path);

        osvqt_test::addTinyImages(vault, "test", 1);

        // Get the node
        auto nodes = vault.list("");
        QCOMPARE((int)nodes.size(), 1);

        QTemporaryDir export_dir;
        QVERIFY(export_dir.isValid());

        // Try exporting to a non-existent path (outside export directory)
        QString evilExportDir = export_dir.path() + "/../nonexistent/etc";

        ExportController controller;
        QSignalSpy finishedSpy(&controller, &ExportController::finished);

        QList<quintptr> nodeIds;
        nodeIds.append(reinterpret_cast<quintptr>(nodes[0]));

        controller.setVault(&vault);
        controller.startExport(evilExportDir, nodeIds);

        // Wait for completion
        if (finishedSpy.isEmpty()) {
            QEventLoop loop;
            QTimer timer;
            timer.setSingleShot(true);
            connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            connect(&controller, &ExportController::finished, &loop, &QEventLoop::quit);
            timer.start(5000);
            loop.exec();
        }

        QVERIFY(!finishedSpy.isEmpty());
        auto args = finishedSpy.takeFirst();
        bool ok = args.at(0).toBool();
        QVERIFY(!ok);  // Export must fail
    }

    void testExportSimpleSuccess()
    {
        // TDD: basic export functionality works
        QTemporaryDir temp_dir;
        QVERIFY(temp_dir.isValid());

        std::string vault_path = (temp_dir.path() + "/test3.osv").toStdString();
        auto vault = osvqt_test::createTestVault(vault_path);

        osvqt_test::addTinyImages(vault, "img", 2);
        auto nodes = vault.list("");
        QCOMPARE((int)nodes.size(), 2);

        QTemporaryDir export_dir;
        QVERIFY(export_dir.isValid());

        ExportController controller;
        QSignalSpy finishedSpy(&controller, &ExportController::finished);
        QSignalSpy progressSpy(&controller, &ExportController::progressUpdated);

        QList<quintptr> nodeIds;
        for (const auto& node : nodes) {
            nodeIds.append(reinterpret_cast<quintptr>(node));
        }

        controller.setVault(&vault);
        controller.startExport(export_dir.path(), nodeIds);

        // Wait for completion
        if (finishedSpy.isEmpty()) {
            QEventLoop loop;
            QTimer timer;
            timer.setSingleShot(true);
            connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            connect(&controller, &ExportController::finished, &loop, &QEventLoop::quit);
            timer.start(5000);
            loop.exec();
        }

        QVERIFY(!finishedSpy.isEmpty());
        auto args = finishedSpy.takeFirst();
        bool ok = args.at(0).toBool();
        QVERIFY(ok);  // Export must succeed
    }

private:
};

QTEST_MAIN(ExportControllerTest)
#include "export_controller_test.moc"
