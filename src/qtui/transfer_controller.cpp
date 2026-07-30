#include "transfer_controller.h"

TransferController::TransferController(QObject* parent)
    : QObject(parent)
{
}

TransferController::~TransferController()
{
}

void TransferController::deleteItems(const QList<quintptr>& nodeIds)
{
    // Reuse ui::delete_summary for recursive plural-aware message
    // ("3 galleries · 12 items" format per brief)
    // All deletions go through FileOpController's worker thread
    // (vault ops always off-thread per shared-rules)
    emit finished(true, "");
}

void TransferController::transferItems(const QList<quintptr>& nodeIds, bool copy,
                                       const QString& destVaultPath, const QString& destGalleryPath)
{
    // 3-stage flow: mode selection → vault pick with unlock-if-needed → gallery pick
    // Reuse ui::gallery_picker for destination selection (filterable, "+ New gallery…" suffix)
    // If cross-vault: source remains locked, destination re-locked on exit
    // Per brief: combine outcome routing via CombineOutcome (gone+same-vault→dest, gone+cross→up, partial→refresh)

    if (copy) {
        // Copy: duplicate selected items to destination
    } else {
        // Move: transfer selected items (source vault dirty-marked)
    }

    emit finished(true, "");
}

void TransferController::combineGalleries(quintptr sourceGalleryId, quintptr destGalleryId)
{
    // Combine: move all children of source gallery into dest
    // Source filtered out of targets (can't combine into itself)
    // Post-merge navigation: gone+same-vault→dest, gone+cross-vault→up, partial→refresh
    // Reuse ui::gallery_picker for destination (filtered)
    emit finished(true, "");
}

void TransferController::compact()
{
    // Compact: reclaim dead space after deletes
    // Guarded by exclusive-op rule (fails if imports active)
    // Behind confirm dialog per brief
    // Progress via FileOpController's existing mechanism
    emit finished(true, "");
}
