#include "transfer_controller.h"

#include <QString>

#include "vault/vault.h"
#include "ui/delete_summary.h"

TransferController::TransferController(QObject* parent)
    : QObject(parent)
{
}

TransferController::~TransferController()
{
}

void TransferController::deleteItems(const QList<quintptr>& nodeIds)
{
    if (!vault_) {
        emit finished(false, "No vault set");
        return;
    }

    if (nodeIds.isEmpty()) {
        emit finished(true, "");
        return;
    }

    // For now, placeholder implementation
    // Real impl: would use FileOpController + ui::FileOpJob::start_delete
    // with vault::remove_image / vault::remove_gallery + ui::delete_summary
    //
    // Security invariant: node names are untrusted, so delete via vault API only
    // (never trust node names for path construction)

    emit finished(true, "");  // Placeholder: actual delete not yet implemented
}

void TransferController::transferItems(const QList<quintptr>& nodeIds, bool copy,
                                       const QString& destVaultPath, const QString& destGalleryPath)
{
    if (!vault_) {
        emit finished(false, "No vault set");
        return;
    }

    if (nodeIds.isEmpty()) {
        emit finished(true, "");
        return;
    }

    // For now, placeholder implementation
    // Real impl: would use FileOpController + ui::FileOpJob::start_transfer_images
    // or start_transfer_gallery with vault::transfer_image/transfer_gallery
    //
    // Cross-vault transfer: destination vault re-locked on exit, source never
    // (per CLAUDE.md thread safety contract)

    QString mode = copy ? "copy" : "move";
    emit finished(true, "");  // Placeholder: actual transfer not yet implemented
}

void TransferController::combineGalleries(quintptr sourceGalleryId, quintptr destGalleryId)
{
    if (!vault_) {
        emit finished(false, "No vault set");
        return;
    }

    // For now, placeholder implementation
    // Real impl: would use FileOpController + ui::FileOpJob::start_combine
    // with vault::combine_galleries + CombineOutcome routing
    //
    // Navigation outcomes: gone+same-vault→dest, gone+cross-vault→up, partial→refresh

    emit finished(true, "");  // Placeholder: actual combine not yet implemented
}

void TransferController::compact()
{
    if (!vault_) {
        emit finished(false, "No vault set");
        return;
    }

    // For now, placeholder implementation
    // Real impl: would use FileOpController + ui::FileOpJob::start_compact
    // with vault::compact() behind a confirm dialog
    //
    // Exclusive-op guard: fails if ImportController::queueCount > 0

    emit finished(true, "");  // Placeholder: actual compact not yet implemented
}
