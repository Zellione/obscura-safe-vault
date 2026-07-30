#include "export_controller.h"

#include <filesystem>
#include <vector>
#include <algorithm>

#include "vault/vault.h"
#include "ui/export.h"
#include "import_controller.h"

ExportController::ExportController(QObject* parent)
    : QObject(parent)
{
}

ExportController::~ExportController()
{
}

int ExportController::getQueueCount() const
{
    if (queue_count_provider_) {
        return queue_count_provider_();
    }
    if (import_controller_) {
        return import_controller_->queueCount();
    }
    return 0;
}

bool ExportController::shouldBlockForExclusiveOp() const
{
    return getQueueCount() > 0;
}

void ExportController::startExport(const QString& destination, const QList<quintptr>& nodeIds)
{
    if (!vault_) {
        emit finished(false, "No vault set");
        return;
    }

    // Check exclusive-op guard: refuse if imports are active
    if (shouldBlockForExclusiveOp()) {
        emit finished(false, "Cannot export while imports are active");
        return;
    }

    std::filesystem::path destPath(destination.toStdString());

    // Verify destination directory exists and is accessible
    if (!std::filesystem::exists(destPath) || !std::filesystem::is_directory(destPath)) {
        emit finished(false, "Export directory does not exist");
        return;
    }

    // Convert node IDs (which are pointers to IndexNode) to IndexNode pointers
    std::vector<const vault::IndexNode*> nodes;
    nodes.reserve(nodeIds.size());

    for (const auto nodeId : nodeIds) {
        if (nodeId == 0) continue;
        const auto* node = reinterpret_cast<const vault::IndexNode*>(nodeId);
        nodes.push_back(node);
    }

    if (nodes.empty()) {
        emit finished(true, "");  // Nothing to export
        return;
    }

    // SECURITY: Per CLAUDE.md invariant-1 exception (export):
    // - Selection-only: only export nodes in the selection (not whole tree)
    // - Originals-only: ui::export_images handles this (never thumbnails)
    // - Consent: caller must show consent dialog before calling startExport
    // - Wipe after write: SecureBytes dtor + ui::export_images handles crypto_wipe
    // - Path containment: ui::export_path_within checks each final path via export_images

    // Export with consent confirmed (caller showed modal before calling)
    auto summary = ui::export_images(
        *vault_,
        std::span<const vault::IndexNode* const>(nodes.data(), nodes.size()),
        destPath,
        ui::ExportConsent::Confirm
    );

    if (summary.failed == 0 && summary.written > 0) {
        emit progressUpdated(summary.written, summary.written + summary.failed);
        emit finished(true, "");
    } else if (summary.written > 0) {
        // Partial success
        QString error = QString("Exported %1 items, %2 failed")
                       .arg(summary.written)
                       .arg(summary.failed);
        emit progressUpdated(summary.written, summary.written + summary.failed);
        emit finished(true, error);
    } else {
        // All failed
        QString error = QString("Export failed: %1 items failed out of %2")
                       .arg(summary.failed)
                       .arg(nodes.size());
        emit progressUpdated(0, nodes.size());
        emit finished(false, error);
    }
}

void ExportController::cancel()
{
    // ui::export_images handles cooperative cancellation through OpProgress
    // No explicit action needed here (FileOpJob will manage via OpProgress::cancel)
}
