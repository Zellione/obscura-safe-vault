#pragma once

#include <QObject>
#include <QString>
#include <QList>

// Transfer controller: handles move/copy/delete/combine/compact operations
// Guarded by exclusive-op rule: all blocked while imports active (ImportController::queueCount > 0)
class TransferController : public QObject {
    Q_OBJECT

public:
    explicit TransferController(QObject* parent = nullptr);
    ~TransferController();

    // Delete selected items with confirm dialog
    // nodeIds: selected node IDs to delete
    Q_INVOKABLE void deleteItems(const QList<quintptr>& nodeIds);

    // Move/copy selected items
    Q_INVOKABLE void transferItems(const QList<quintptr>& nodeIds, bool copy,
                                   const QString& destVaultPath, const QString& destGalleryPath);

    // Combine source gallery into destination
    Q_INVOKABLE void combineGalleries(quintptr sourceGalleryId, quintptr destGalleryId);

    // Compact vault (reclaim dead space after deletes)
    Q_INVOKABLE void compact();

signals:
    // Emitted when operation completes
    void finished(bool ok, QString error);

private:
    // FileOpController is delegated to for actual work
};
