#pragma once

#include <QObject>
#include <QString>
#include <QList>

namespace vault { class Vault; }

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
};
