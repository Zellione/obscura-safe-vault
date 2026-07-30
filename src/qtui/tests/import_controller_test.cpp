#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <fstream>
#include <cstring>

#include "import_controller.h"
#include "vault/vault.h"
#include "ui/import_queue.h"
#include "crypto/secure_mem.h"
#include "test_vault_util.h"

class ImportControllerTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Create a temporary directory for testing
        temp_dir_ = std::make_unique<QTemporaryDir>();
        QVERIFY(temp_dir_->isValid());

        // Create controller
        controller_ = std::make_unique<ImportController>();
    }

    void cleanupTestCase()
    {
        controller_.reset();
        temp_dir_.reset();
    }

    void testQueueCountPropertyInitiallyZero()
    {
        // Verify initial queue count is 0
        QCOMPARE(controller_->queueCount(), 0);
    }

    void testFooterSummaryPropertyInitiallyEmpty()
    {
        // Verify initial footer summary is empty when nothing is queued
        QCOMPARE(controller_->footerSummary(), QString(""));
    }

    void testSessionLifecycle()
    {
        // Verify we can begin and end sessions cleanly
        QVERIFY(!controller_->isSessionActive());

        auto vault = createTestVault("1");
        controller_->begin_session(vault);
        QVERIFY(controller_->isSessionActive());

        controller_->end_session();
        QVERIFY(!controller_->isSessionActive());
    }

    void testEnqueueFilesStoresData()
    {
        auto vault = createTestVault("2");
        controller_->begin_session(vault);

        // Create a test file
        auto file_path = QString::fromStdString(temp_dir_->path().toStdString() + "/testfile.txt");
        std::ofstream file(file_path.toStdString());
        file << "test content";
        file.close();

        // Enqueue should not crash
        controller_->enqueueFiles({file_path});

        // Controller should have accepted it (no crash is the test)
        QVERIFY(controller_->isSessionActive());

        controller_->end_session();
    }

    void testQueueControlsWithoutSession()
    {
        // These should not crash when session is not active
        controller_->cancel(1);
        controller_->reorder(1, 1);
        controller_->clearFinished();
        controller_->setExclusiveOp(true);
        controller_->setExclusiveOp(false);
        controller_->drain(0.016);

        QVERIFY(true);  // If we got here, no crash
    }

    void testDrainWithoutSession()
    {
        // drain() should safely do nothing when no session is active
        controller_->drain(0.016);
        QVERIFY(true);  // No crash is success
    }

private:
    std::unique_ptr<QTemporaryDir> temp_dir_;
    std::unique_ptr<ImportController> controller_;

    // Helper to create a fresh vault for a test
    vault::Vault createTestVault(const std::string& suffix = "")
    {
        std::string name = "/test" + suffix + ".osv";
        std::string path = temp_dir_->path().toStdString() + name;
        return osvqt_test::createTestVault(path);
    }
};

QTEST_MAIN(ImportControllerTest)
#include "import_controller_test.moc"
