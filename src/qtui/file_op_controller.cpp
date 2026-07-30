#include "file_op_controller.h"

#include <QMetaObject>
#include <QtCore/qloggingcategory.h>
#include <thread>
#include <chrono>

Q_LOGGING_CATEGORY(lcFileOpController, "osv.file_op_controller")

// Worker runnable: executes a file operation on a background thread.
// Runs the provided function with the job, monitors progress, and signals
// completion back to the controller via queued connections.
class FileOpWorker : public QRunnable {
public:
    FileOpWorker(FileOpController* controller, std::function<void(ui::FileOpJob&)> fn)
        : controller_(controller), fn_(fn)
    {
    }

    void run() override
    {
        if (!controller_ || !fn_) {
            return;
        }

        try {
            // Run the operation function on this worker thread
            fn_(controller_->job_);

            // Monitor progress while the job is active
            int last_done = 0;
            int last_total = 0;
            while (controller_->job_.active()) {
                const int current_done = controller_->job_.done();
                const int current_total = controller_->job_.total();

                if (current_done != last_done || current_total != last_total) {
                    controller_->done_.store(current_done, std::memory_order_release);
                    controller_->total_.store(current_total, std::memory_order_release);
                    QMetaObject::invokeMethod(controller_, &FileOpController::onProgress,
                                              Qt::QueuedConnection);
                    last_done = current_done;
                    last_total = current_total;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            // Job is done; take the outcome
            auto outcome = controller_->job_.take_outcome();

            // Update final progress
            if (outcome.has_value()) {
                controller_->done_.store(outcome->done, std::memory_order_release);
                controller_->total_.store(outcome->total, std::memory_order_release);
                QMetaObject::invokeMethod(controller_, &FileOpController::onProgress,
                                          Qt::QueuedConnection);

                // Emit finished signal
                const bool ok = outcome->ok;
                const QString error = QString::fromStdString(outcome->error);
                auto ctrl = controller_;
                QMetaObject::invokeMethod(
                    controller_, [ctrl, ok, error]() { ctrl->onJobFinished(ok, error); },
                    Qt::QueuedConnection);
            } else {
                // Shouldn't happen, but handle gracefully
                auto ctrl = controller_;
                QMetaObject::invokeMethod(
                    controller_, [ctrl]() { ctrl->onJobFinished(false, "Operation failed"); },
                    Qt::QueuedConnection);
            }
        } catch (const std::exception& e) {
            qCWarning(lcFileOpController) << "Exception in file operation worker:" << e.what();
            auto ctrl = controller_;
            const QString error_msg = QString::fromStdString(std::string(e.what()));
            QMetaObject::invokeMethod(
                controller_, [ctrl, error_msg]() { ctrl->onJobFinished(false, error_msg); },
                Qt::QueuedConnection);
        }
    }

private:
    FileOpController* controller_;
    std::function<void(ui::FileOpJob&)> fn_;
};

FileOpController::FileOpController(QObject* parent)
    : QObject(parent)
{
    qCDebug(lcFileOpController) << "FileOpController constructed";
}

FileOpController::~FileOpController()
{
    qCDebug(lcFileOpController) << "FileOpController destroyed";
}

bool FileOpController::start(std::function<void(ui::FileOpJob&)> fn)
{
    if (active_.load(std::memory_order_acquire)) {
        qCDebug(lcFileOpController) << "start() ignored: job already active";
        return false;
    }

    if (!fn) {
        qCWarning(lcFileOpController) << "start() called with null function";
        return false;
    }

    // Set active flag
    active_.store(true, std::memory_order_release);
    done_.store(0, std::memory_order_release);
    total_.store(0, std::memory_order_release);

    qCDebug(lcFileOpController) << "Starting file operation";

    // Create and queue worker
    auto* worker = new FileOpWorker(this, fn);
    worker->setAutoDelete(true);
    pool_.start(worker);

    return true;
}

void FileOpController::cancel()
{
    qCDebug(lcFileOpController) << "Requesting cancel";
    job_.cancel();
}

void FileOpController::onProgress()
{
    qCDebug(lcFileOpController) << "Progress: done=" << done() << "total=" << total();
    emit progressChanged();
}

void FileOpController::onJobFinished(bool ok, const QString& error)
{
    qCDebug(lcFileOpController) << "Job finished: ok=" << ok << "error=" << error;

    active_.store(false, std::memory_order_release);
    emit progressChanged();
    emit finished(ok, error);
}
