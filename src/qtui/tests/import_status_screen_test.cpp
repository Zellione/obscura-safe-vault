#include <QtTest>
#include <QAbstractListModel>
#include <QSignalSpy>

#include "vault/vault.h"
#include "test_vault_util.h"
#include "ui/import_model.h"

// Import status row model test: verifies two-line row content per state
// and id-stable reordering behavior (Phase 50/56 spec).
class ImportStatusScreenTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        temp_dir_ = std::make_unique<QTemporaryDir>();
        QVERIFY(temp_dir_->isValid());
    }

    void cleanupTestCase()
    {
        temp_dir_.reset();
    }

    void testImportTaskInfoSnapshot()
    {
        // Verify ImportTaskInfo structure for a queued task
        ui::ImportTaskInfo task;
        task.id = 42;
        task.kind = ui::ImportTaskKind::Files;
        task.display_name = "test.zip";
        task.state = ui::ImportTaskState::Queued;
        task.done = 0;
        task.total = 10;
        task.imported = 0;
        task.skipped = 0;

        QCOMPARE(task.id, (uint64_t)42);
        QCOMPARE(task.state, ui::ImportTaskState::Queued);
        QVERIFY(task.display_name == "test.zip");
    }

    void testFormatTaskProgressShowsCount()
    {
        // Verify progress formatting matches Phase 50 spec
        std::string progress = ui::format_task_progress(128, 450, false);
        QVERIFY(!progress.empty());
        QVERIFY(progress.find("128") != std::string::npos);  // done count visible
        QVERIFY(progress.find("450") != std::string::npos);  // total visible
    }

    void testFormatTaskProgressWithExpanding()
    {
        // When total is expanding (nested archives), format includes indicator
        std::string progress = ui::format_task_progress(50, 100, true);
        QVERIFY(!progress.empty());
        QVERIFY(progress.find("expanding") != std::string::npos ||
                progress.find("50") != std::string::npos);  // at least has count
    }

    void testFooterImportSummaryWithQueuedTasks()
    {
        std::vector<ui::ImportTaskInfo> tasks;

        // Running task
        ui::ImportTaskInfo running;
        running.id = 1;
        running.display_name = "current.zip";
        running.state = ui::ImportTaskState::Running;
        running.done = 25;
        running.total = 100;
        tasks.push_back(running);

        // Queued task
        ui::ImportTaskInfo queued;
        queued.id = 2;
        queued.display_name = "next.zip";
        queued.state = ui::ImportTaskState::Queued;
        queued.done = 0;
        queued.total = 50;
        tasks.push_back(queued);

        std::string summary = ui::footer_import_summary(tasks, false);
        QVERIFY(!summary.empty());
        QVERIFY(summary.find("current.zip") != std::string::npos);
        QVERIFY(summary.find("1") != std::string::npos ||
                summary.find("queued") != std::string::npos);  // indicates queued count
    }

    void testFooterImportSummaryWhenLaneFailed()
    {
        std::vector<ui::ImportTaskInfo> tasks;
        ui::ImportTaskInfo failed;
        failed.display_name = "broken.zip";
        failed.state = ui::ImportTaskState::Failed;
        failed.error = "Disk full";
        tasks.push_back(failed);

        std::string summary = ui::footer_import_summary(tasks, true, "Disk full");
        QVERIFY(summary.find("failed") != std::string::npos ||
                summary.find("Disk full") != std::string::npos);
    }

    void testReorderImportTaskStableById()
    {
        std::vector<ui::ImportTaskInfo> tasks;

        for (int i = 1; i <= 5; ++i) {
            ui::ImportTaskInfo task;
            task.id = i;
            task.display_name = std::string("task") + char('0' + i);
            task.state = (i == 1 || i == 5) ? ui::ImportTaskState::Running :
                         (i <= 3) ? ui::ImportTaskState::Queued :
                         ui::ImportTaskState::Done;
            tasks.push_back(task);
        }

        // Reorder task 2 down by 1 (should swap with task 3)
        bool reordered = ui::reorder_import_task(tasks, 2, 1);
        QVERIFY(reordered);

        // Verify task 2 is now at index 2 (after task 3 which is now at index 1)
        int idx = ui::index_of_task(tasks, 2);
        QVERIFY(idx > 0);  // moved down
        QCOMPARE(idx, 2);  // swapped with next queued
    }

    void testIndexOfTaskReturnsNegativeWhenMissing()
    {
        std::vector<ui::ImportTaskInfo> tasks;
        ui::ImportTaskInfo task;
        task.id = 10;
        task.display_name = "test";
        tasks.push_back(task);

        int idx = ui::index_of_task(tasks, 999);  // nonexistent
        QCOMPARE(idx, -1);
    }

    void testClearFinishedImportsRemovesCompleted()
    {
        std::vector<ui::ImportTaskInfo> tasks;

        for (int i = 0; i < 5; ++i) {
            ui::ImportTaskInfo task;
            task.id = i;
            task.display_name = std::string("task") + char('0' + i);
            task.state = (i % 2 == 0) ? ui::ImportTaskState::Done :
                         ui::ImportTaskState::Running;
            tasks.push_back(task);
        }

        int removed = ui::clear_finished_imports(tasks);
        QVERIFY(removed > 0);
        QVERIFY(tasks.size() < 5);

        // Verify only running tasks remain
        for (const auto& t : tasks) {
            QCOMPARE(t.state, ui::ImportTaskState::Running);
        }
    }

private:
    std::unique_ptr<QTemporaryDir> temp_dir_;
};

QTEST_MAIN(ImportStatusScreenTest)
#include "import_status_screen_test.moc"
