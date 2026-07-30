#pragma once

#include <QObject>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QRunnable>
#include <functional>
#include <atomic>

#include "ui/file_op_job.h"

// Adapter over ui::FileOpJob providing a QML-friendly interface for vault
// file operations (export, delete, transfer, compact).
//
// Threading model:
// - start(fn) queues fn to run on a background thread (via ui::FileOpJob's worker)
// - fn receives the job and calls its start_export/start_delete/etc methods
// - Progress signals are emitted on the GUI thread via queued connections
// - finished signal includes outcome (ok, error message)
class FileOpController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY progressChanged)
    Q_PROPERTY(int done READ done NOTIFY progressChanged)
    Q_PROPERTY(int total READ total NOTIFY progressChanged)

public:
    explicit FileOpController(QObject* parent = nullptr);
    ~FileOpController();

    // Start a file operation. fn runs on the job's worker thread and receives
    // the FileOpJob for calling start_export/start_delete/etc.
    // Returns false if a job is already active, true if the job started.
    Q_INVOKABLE bool start(std::function<void(ui::FileOpJob&)> fn);

    // Request a cooperative cancellation (items committed so far remain).
    Q_INVOKABLE void cancel();

    // Properties: valid while active()
    [[nodiscard]] bool active() const { return active_.load(std::memory_order_acquire); }
    [[nodiscard]] int done() const { return done_.load(std::memory_order_acquire); }
    [[nodiscard]] int total() const { return total_.load(std::memory_order_acquire); }

signals:
    // Emitted on GUI thread when progress changes (done, total, or active state)
    void progressChanged();

    // Emitted on GUI thread when operation completes.
    // ok: true if completed successfully (or clean cancel), false if error.
    // error: empty if ok, error description otherwise.
    void finished(bool ok, QString error);

private:
    friend class FileOpWorker;

    // Called from worker via queued connection to update progress
    void onProgress();

    // Called from worker via queued connection when job completes
    void onJobFinished(bool ok, const QString& error);

    ui::FileOpJob job_;
    std::atomic<bool> active_{false};
    std::atomic<int> done_{0};
    std::atomic<int> total_{0};

    QThreadPool pool_;
};
