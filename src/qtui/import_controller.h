#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <memory>
#include <atomic>

#include "ui/import_queue.h"

namespace vault { class Vault; }

// Adapter over ui::ImportQueue providing a QML-friendly interface for vault
// imports (files, archives, folders, volume sets).
//
// Threading model:
// - ui::ImportQueue runs a worker thread for import operations
// - Progress updates are pumped via drain(dt) called each frame from GUI thread
// - Signals are emitted on the GUI thread
// - Session lifecycle: begin_session (binds to vault) -> enqueue* methods -> drain per frame -> end_session
//
// Cross-workstream contracts:
// - queueCount Q_PROPERTY: WS1's autolock reads this to suppress auto-lock while importing
// - footerSummary Q_PROPERTY: StatusController displays this in the footer
// - setExclusiveOp(bool): FileOpController calls this to block new imports during vault ops
class ImportController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int queueCount READ queueCount NOTIFY queueChanged)
    Q_PROPERTY(QString footerSummary READ footerSummary NOTIFY queueChanged)

public:
    explicit ImportController(QObject* parent = nullptr);
    ~ImportController();

    // Session lifecycle (main thread)
    void begin_session(vault::Vault& vault);
    void end_session();
    [[nodiscard]] bool isSessionActive() const { return session_active_.load(std::memory_order_acquire); }

    // File picker invocables (main thread)
    // These would typically call SDL3's file dialog and then enqueue*
    Q_INVOKABLE void pickFiles();      // Ctrl+O — multi-select, stem-named galleries
    Q_INVOKABLE void pickFolders();    // O — hierarchy mirrored
    Q_INVOKABLE void pickArchives();   // Z — zip/cbz/7z/rar/tar + cb* variants

    // Enqueue operations (main thread; call after begin_session)
    void enqueueFiles(const QList<QString>& paths);
    void enqueueArchive(const QString& path, const QString& gallery_name = "");
    void enqueueFolder(const QString& path, const QString& gallery_name = "");
    void enqueueVolumeSet(const QList<QString>& volume_paths, const QString& stem,
                         const QString& gallery_name = "");

    // Queue control (main thread)
    Q_INVOKABLE void cancel(uint64_t task_id);
    Q_INVOKABLE void reorder(uint64_t task_id, int delta);
    Q_INVOKABLE void clearFinished();

    // Exclusive op guard (called by FileOpController)
    void setExclusiveOp(bool held);

    // Per-frame pump (main thread; call from render loop)
    void drain(double dt);

    // Properties (main thread / render)
    [[nodiscard]] int queueCount() const { return queue_count_.load(std::memory_order_acquire); }
    [[nodiscard]] QString footerSummary() const;

signals:
    // Emitted when queue state changes (item queued, started, finished, etc.)
    void queueChanged();

    // Emitted when an archive needs a password
    void passwordNeeded(QString archiveName);

    // Emitted when a lane error occurs (e.g., disk full, write permission)
    void laneFailure(QString error);

private:
    vault::Vault* vault_ = nullptr;
    std::unique_ptr<ui::ImportQueue> queue_;

    std::atomic<bool> session_active_{false};
    std::atomic<int> queue_count_{0};
    QString cached_footer_;

    void updateFromSnapshot();
};
