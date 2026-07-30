#include <QtTest>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QTimer>
#include <QGuiApplication>
#include <thread>

#include "transfer_controller.h"
#include "file_op_controller.h"
#include "test_vault_util.h"

class TransferControllerTest : public QObject {
    Q_OBJECT

private slots:
    void testDeleteRefusesWhenImportActive()
    {
        // GUARD: delete refuses when queue count > 0
        QTemporaryDir temp_dir;
        QVERIFY(temp_dir.isValid());

        std::string vault_path = (temp_dir.path() + "/test_guard.osv").toStdString();
        auto vault = osvqt_test::createTestVault(vault_path);

        osvqt_test::addTinyImages(vault, "img", 1);
        auto nodes = vault.list("");
        QCOMPARE((int)nodes.size(), 1);

        TransferController controller;
        QSignalSpy finishedSpy(&controller, &TransferController::finished);

        controller.setVault(&vault);

        // Set queue count to 1 (simulate active imports)
        controller.setQueueCountProvider([]() { return 1; });

        // Try to delete - should refuse immediately
        controller.deleteItems({reinterpret_cast<quintptr>(nodes[0])});

        // Signal should arrive immediately with error
        QCOMPARE(finishedSpy.count(), 1);
        auto args = finishedSpy.takeFirst();
        bool ok = args.at(0).toBool();
        QVERIFY(!ok);  // Should fail
        QString error = args.at(1).toString();
        QVERIFY(!error.isEmpty());  // Error message should be non-empty
    }

    void testDeleteRunsOnWorkerThread()
    {
        // THREADING: delete runs work on non-GUI thread
        QGuiApplication::processEvents();

        QTemporaryDir temp_dir;
        QVERIFY(temp_dir.isValid());

        std::string vault_path = (temp_dir.path() + "/test_thread.osv").toStdString();
        auto vault = osvqt_test::createTestVault(vault_path);

        osvqt_test::addTinyImages(vault, "img", 1);
        auto nodes = vault.list("");

        FileOpController file_op;
        TransferController controller;
        QSignalSpy finishedSpy(&controller, &TransferController::finished);

        const auto gui_thread_id = std::this_thread::get_id();

        controller.setVault(&vault);
        controller.setFileOpController(&file_op);
        controller.setQueueCountProvider([]() { return 0; });  // No active imports

        controller.deleteItems({reinterpret_cast<quintptr>(nodes[0])});

        // Wait for finished signal
        for (int i = 0; i < 100 && finishedSpy.count() == 0; ++i) {
            QGuiApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        QVERIFY(!finishedSpy.isEmpty());

        // Check that work ran on different thread
        QVERIFY(controller.last_worker_thread_id_ != std::thread::id{});
        QVERIFY(controller.last_worker_thread_id_ != gui_thread_id);

        // finished() signal arrived via queued connection
        QCOMPARE(finishedSpy.count(), 1);
    }

    void testCompactRefusesWhenImportActive()
    {
        // GUARD: compact refuses when queue count > 0
        QTemporaryDir temp_dir;
        QVERIFY(temp_dir.isValid());

        std::string vault_path = (temp_dir.path() + "/test_compact_guard.osv").toStdString();
        auto vault = osvqt_test::createTestVault(vault_path);

        FileOpController file_op;
        TransferController controller;
        QSignalSpy finishedSpy(&controller, &TransferController::finished);

        controller.setVault(&vault);
        controller.setFileOpController(&file_op);
        controller.setQueueCountProvider([]() { return 5; });  // 5 items queued

        controller.compact();

        QCOMPARE(finishedSpy.count(), 1);
        auto args = finishedSpy.takeFirst();
        bool ok = args.at(0).toBool();
        QVERIFY(!ok);
    }

    void testTransferItemsSignalsCompletion()
    {
        QGuiApplication::processEvents();

        QTemporaryDir temp_dir;
        QVERIFY(temp_dir.isValid());

        std::string vault_path = (temp_dir.path() + "/test_transfer.osv").toStdString();
        auto vault = osvqt_test::createTestVault(vault_path);

        osvqt_test::addTinyImages(vault, "img", 1);
        auto nodes = vault.list("");
        QCOMPARE((int)nodes.size(), 1);

        FileOpController file_op;
        TransferController controller;
        QSignalSpy finishedSpy(&controller, &TransferController::finished);

        controller.setVault(&vault);
        controller.setFileOpController(&file_op);
        controller.setQueueCountProvider([]() { return 0; });

        controller.transferItems({reinterpret_cast<quintptr>(nodes[0])}, true, "", "");

        // Wait for completion
        for (int i = 0; i < 100 && finishedSpy.count() == 0; ++i) {
            QGuiApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        QVERIFY(!finishedSpy.isEmpty());
    }

    void testCombineGalleriesSignalsCompletion()
    {
        QGuiApplication::processEvents();

        QTemporaryDir temp_dir;
        QVERIFY(temp_dir.isValid());

        std::string vault_path = (temp_dir.path() + "/test_combine.osv").toStdString();
        auto vault = osvqt_test::createTestVault(vault_path);

        FileOpController file_op;
        TransferController controller;
        QSignalSpy finishedSpy(&controller, &TransferController::finished);

        controller.setVault(&vault);
        controller.setFileOpController(&file_op);
        controller.setQueueCountProvider([]() { return 0; });

        controller.combineGalleries(0, 1);

        // Wait for completion
        for (int i = 0; i < 100 && finishedSpy.count() == 0; ++i) {
            QGuiApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        QVERIFY(!finishedSpy.isEmpty());
    }

    void testTransferMediaFilesMove()
    {
        // Item 1: Media-file transfer — move 2 of 3 images between vaults
        QGuiApplication::processEvents();

        QTemporaryDir temp_dir;
        QVERIFY(temp_dir.isValid());

        // Source vault: 3 images
        std::string src_path = (temp_dir.path() + "/src_media.osv").toStdString();
        auto src_vault = osvqt_test::createTestVault(src_path);
        osvqt_test::addTinyImages(src_vault, "img", 3);

        auto src_nodes = src_vault.list("");
        QCOMPARE((int)src_nodes.size(), 3);

        // Destination vault: empty
        std::string dst_path = (temp_dir.path() + "/dst_media.osv").toStdString();
        auto dst_vault = osvqt_test::createTestVault(dst_path);

        // Move first 2 images
        QList<quintptr> to_move{
            reinterpret_cast<quintptr>(src_nodes[0]),
            reinterpret_cast<quintptr>(src_nodes[1])
        };

        FileOpController file_op;
        TransferController controller;
        QSignalSpy finishedSpy(&controller, &TransferController::finished);

        controller.setVault(&src_vault);
        controller.setFileOpController(&file_op);
        controller.setQueueCountProvider([]() { return 0; });

        controller.transferItems(to_move, false, QString::fromStdString(dst_path), "");

        // Wait for completion
        for (int i = 0; i < 100 && finishedSpy.count() == 0; ++i) {
            QGuiApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        QVERIFY(!finishedSpy.isEmpty());
        auto args = finishedSpy.takeFirst();
        bool ok = args.at(0).toBool();
        QVERIFY(ok);

        // Verify source: 1 image remains (moved removed 2)
        auto src_remaining = src_vault.list("");
        QCOMPARE((int)src_remaining.size(), 1);

        // Verify destination: 2 images added
        auto dst_items = dst_vault.list("");
        QCOMPARE((int)dst_items.size(), 2);
    }

    void testTransferMediaFilesCopy()
    {
        // Item 1: Media-file transfer — copy leaves source intact
        QGuiApplication::processEvents();

        QTemporaryDir temp_dir;
        QVERIFY(temp_dir.isValid());

        // Source vault: 2 images
        std::string src_path = (temp_dir.path() + "/src_copy.osv").toStdString();
        auto src_vault = osvqt_test::createTestVault(src_path);
        osvqt_test::addTinyImages(src_vault, "img", 2);

        auto src_nodes = src_vault.list("");
        QCOMPARE((int)src_nodes.size(), 2);

        // Destination vault: empty
        std::string dst_path = (temp_dir.path() + "/dst_copy.osv").toStdString();
        auto dst_vault = osvqt_test::createTestVault(dst_path);

        // Copy first image
        QList<quintptr> to_copy{reinterpret_cast<quintptr>(src_nodes[0])};

        FileOpController file_op;
        TransferController controller;
        QSignalSpy finishedSpy(&controller, &TransferController::finished);

        controller.setVault(&src_vault);
        controller.setFileOpController(&file_op);
        controller.setQueueCountProvider([]() { return 0; });

        controller.transferItems(to_copy, true, QString::fromStdString(dst_path), "");

        // Wait for completion
        for (int i = 0; i < 100 && finishedSpy.count() == 0; ++i) {
            QGuiApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        QVERIFY(!finishedSpy.isEmpty());
        auto args = finishedSpy.takeFirst();
        bool ok = args.at(0).toBool();
        QVERIFY(ok);

        // Verify source: still has 2 images (copy leaves intact)
        auto src_after = src_vault.list("");
        QCOMPARE((int)src_after.size(), 2);

        // Verify destination: 1 image added
        auto dst_items = dst_vault.list("");
        QCOMPARE((int)dst_items.size(), 1);
    }

    void testCompactSignalsCompletion()
    {
        QGuiApplication::processEvents();

        QTemporaryDir temp_dir;
        QVERIFY(temp_dir.isValid());

        std::string vault_path = (temp_dir.path() + "/test_compact.osv").toStdString();
        auto vault = osvqt_test::createTestVault(vault_path);

        osvqt_test::addTinyImages(vault, "img", 1);

        FileOpController file_op;
        TransferController controller;
        QSignalSpy finishedSpy(&controller, &TransferController::finished);

        controller.setVault(&vault);
        controller.setFileOpController(&file_op);
        controller.setQueueCountProvider([]() { return 0; });

        controller.compact();

        // Wait for completion
        for (int i = 0; i < 100 && finishedSpy.count() == 0; ++i) {
            QGuiApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        QVERIFY(!finishedSpy.isEmpty());
    }
};

QTEST_MAIN(TransferControllerTest)
#include "transfer_controller_test.moc"
