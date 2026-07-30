#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <functional>
#include <thread>

namespace vault { class Vault; }
class FileOpController;
class ImportController;

// Transfer controller: wraps ui::transfer_*, vault::combine_galleries, and vault::compact
// All vault-mutating operations run on a background thread via FileOpController.
//
// Threading contract:
// - Delete, transfer, combine, compact all use FileOpController worker threads
// - Finished signal (queued) indicates completion; caller must NOT touch vault until then
// - Exclusive-op guard: operations fail if ImportController::queueCount > 0
class TransferController : public QObject {
    Q_OBJECT

public:
    explicit TransferController(QObject* parent = nullptr);
    ~TransferController();

    // Set the vault for operations (required before calling any operation)
    void setVault(vault::Vault* vault) { vault_ = vault; }

    // Set dependencies for exclusive-op guard and thread pool
    void setFileOpController(FileOpController* controller) { file_op_controller_ = controller; }
    void setImportController(ImportController* controller) { import_controller_ = controller; }

    // Alternative: inject a queue-count provider (for testing without full ImportController)
    void setQueueCountProvider(std::function<int()> provider) { queue_count_provider_ = provider; }

    // Delete one or more items (images, videos, or galleries and their subtrees)
    // nodeIds are pointers to IndexNode objects
    Q_INVOKABLE void deleteItems(const QList<quintptr>& nodeIds);

    // Transfer (move/copy) items between galleries (same or different vaults)
    // copy=true: copy mode; copy=false: move mode
    Q_INVOKABLE void transferItems(const QList<quintptr>& nodeIds, bool copy,
                                   const QString& destVaultPath, const QString& destGalleryPath);

    // Combine source gallery into destination gallery (recursive merge)
    // sourceGalleryId, destGalleryId: pointers to IndexNode objects
    Q_INVOKABLE void combineGalleries(quintptr sourceGalleryId, quintptr destGalleryId);

    // Compact the vault in-place (reclaim dead space from deletes)
    Q_INVOKABLE void compact();

signals:
    // Emitted when operation completes (ok=true for success, ok=false on error)
    void finished(bool ok, QString error);

private:
    vault::Vault* vault_ = nullptr;  // not owned, set by caller
    FileOpController* file_op_controller_ = nullptr;  // not owned; required for worker threads
    ImportController* import_controller_ = nullptr;  // not owned; used for exclusive-op guard
    std::function<int()> queue_count_provider_;  // alternative to import_controller_ for testing

    // Helper: check if exclusive-op guard should block this operation
    [[nodiscard]] bool shouldBlockForExclusiveOp() const;

    // Helper: get current import queue count (from provider or import_controller_)
    [[nodiscard]] int getQueueCount() const;

    // These are for testing: track last worker thread ID
    friend class TransferControllerTest;
    mutable std::thread::id last_worker_thread_id_;
};
