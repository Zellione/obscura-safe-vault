#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QEventLoop>
#include <QTimer>

#include "export_controller.h"
#include "test_vault_util.h"

// Helper: set up vault and export directory
struct TestExportSetup {
    bool valid = false;
    QTemporaryDir vault_dir;
    QTemporaryDir export_dir;
    vault::Vault vault;

    static TestExportSetup create() {
        TestExportSetup setup;
        if (!setup.vault_dir.isValid() || !setup.export_dir.isValid()) {
            return setup;
        }
        try {
            std::string vault_path = (setup.vault_dir.path() + "/test.osv").toStdString();
            setup.vault = osvqt_test::createTestVault(vault_path);
            setup.valid = true;
        } catch (const std::exception& e) {
            return setup;
        }
        return setup;
    }
};

// Helper: wait for export completion with timeout
static void waitForExportCompletion(QSignalSpy& finishedSpy, ExportController& controller, int timeoutMs = 5000)
{
    if (finishedSpy.isEmpty()) {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(&controller, &ExportController::finished, &loop, &QEventLoop::quit);
        timer.start(timeoutMs);
        loop.exec();
    }
}

class ExportControllerTest : public QObject {
    Q_OBJECT

private slots:
    void testExportedBytesMatchOriginals()
    {
        // TDD: real export writes exact decrypted bytes
        auto setup = TestExportSetup::create();
        QVERIFY(setup.valid);

        // Add a test image to the vault
        osvqt_test::addTinyImages(setup.vault, "test", 1);

        // Find the added image in the index
        auto nodes = setup.vault.list("");  // "" = root gallery
        QCOMPARE((int)nodes.size(), 1);
        const auto& image_node = nodes[0];
        QVERIFY(!image_node->is_gallery());

        // Export the image
        ExportController controller;
        QSignalSpy finishedSpy(&controller, &ExportController::finished);
        QList<quintptr> nodeIds;
        nodeIds.append(reinterpret_cast<quintptr>(image_node));

        controller.setVault(&setup.vault);
        controller.startExport(setup.export_dir.path(), nodeIds);

        waitForExportCompletion(finishedSpy, controller);

        QVERIFY(!finishedSpy.isEmpty());
        auto args = finishedSpy.takeFirst();
        bool ok = args.at(0).toBool();
        QVERIFY(ok);  // Export must succeed

        // Verify exported file exists and matches original
        QString exported_path = setup.export_dir.path() + "/" +
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
        auto setup = TestExportSetup::create();
        QVERIFY(setup.valid);

        osvqt_test::addTinyImages(setup.vault, "test", 1);

        // Get the node
        auto nodes = setup.vault.list("");
        QCOMPARE((int)nodes.size(), 1);

        // Try exporting to a non-existent path (outside export directory)
        QString evilExportDir = setup.export_dir.path() + "/../nonexistent/etc";

        ExportController controller;
        QSignalSpy finishedSpy(&controller, &ExportController::finished);
        QList<quintptr> nodeIds;
        nodeIds.append(reinterpret_cast<quintptr>(nodes[0]));

        controller.setVault(&setup.vault);
        controller.startExport(evilExportDir, nodeIds);

        waitForExportCompletion(finishedSpy, controller);

        QVERIFY(!finishedSpy.isEmpty());
        auto args = finishedSpy.takeFirst();
        bool ok = args.at(0).toBool();
        QVERIFY(!ok);  // Export must fail
    }

    void testExportSimpleSuccess()
    {
        // TDD: basic export functionality works
        auto setup = TestExportSetup::create();
        QVERIFY(setup.valid);

        osvqt_test::addTinyImages(setup.vault, "img", 2);
        auto nodes = setup.vault.list("");
        QCOMPARE((int)nodes.size(), 2);

        ExportController controller;
        QSignalSpy finishedSpy(&controller, &ExportController::finished);
        QSignalSpy progressSpy(&controller, &ExportController::progressUpdated);

        QList<quintptr> nodeIds;
        for (const auto& node : nodes) {
            nodeIds.append(reinterpret_cast<quintptr>(node));
        }

        controller.setVault(&setup.vault);
        controller.startExport(setup.export_dir.path(), nodeIds);

        waitForExportCompletion(finishedSpy, controller);

        QVERIFY(!finishedSpy.isEmpty());
        auto args = finishedSpy.takeFirst();
        bool ok = args.at(0).toBool();
        QVERIFY(ok);  // Export must succeed
    }

private:
};

QTEST_MAIN(ExportControllerTest)
#include "export_controller_test.moc"
