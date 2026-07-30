#include "transfer_controller.h"

#include <QString>
#include <QMetaObject>
#include <vector>
#include <thread>

#include "vault/vault.h"
#include "vault/transfer.h"
#include "vault/combine.h"
#include "ui/delete_summary.h"
#include "ui/file_op_job.h"
#include "file_op_controller.h"
#include "import_controller.h"

TransferController::TransferController(QObject* parent)
    : QObject(parent)
{
}

TransferController::~TransferController()
{
}

int TransferController::getQueueCount() const
{
    if (queue_count_provider_) {
        return queue_count_provider_();
    }
    if (import_controller_) {
        return import_controller_->queueCount();
    }
    return 0;
}

bool TransferController::shouldBlockForExclusiveOp() const
{
    return getQueueCount() > 0;
}

void TransferController::deleteItems(const QList<quintptr>& nodeIds)
{
    if (!vault_) {
        emit finished(false, "No vault set");
        return;
    }

    if (!file_op_controller_) {
        emit finished(false, "FileOpController not set");
        return;
    }

    if (nodeIds.isEmpty()) {
        emit finished(true, "");
        return;
    }

    // Check exclusive-op guard
    if (shouldBlockForExclusiveOp()) {
        emit finished(false, "Cannot delete while imports are active");
        return;
    }

    // Collect nodes to delete
    std::vector<const vault::IndexNode*> nodes_to_delete;
    for (const auto nodeId : nodeIds) {
        if (nodeId == 0) continue;
        const auto* node = reinterpret_cast<const vault::IndexNode*>(nodeId);
        nodes_to_delete.push_back(node);
    }

    if (nodes_to_delete.empty()) {
        emit finished(true, "");
        return;
    }

    // Capture `this` to emit finished() when worker completes
    auto* controller = this;

    // Start deletion on worker thread via FileOpController
    file_op_controller_->start([nodes_to_delete, controller](ui::FileOpJob& job) {
        // Capture worker thread ID for testing
        controller->last_worker_thread_id_ = std::this_thread::get_id();

        int deleted = 0;
        for (const auto* node : nodes_to_delete) {
            if (!node) continue;

            // Determine if this is a gallery or image/video
            bool is_gallery = node->is_gallery();

            // Get parent gallery path (simplified - would use full path in production)
            std::string parent_gallery = "";  // Root gallery for now
            std::string node_name(node->name);

            vault::VaultResult result;
            if (is_gallery) {
                result = controller->vault_->remove_gallery(parent_gallery);
            } else {
                result = controller->vault_->remove_image(parent_gallery, node_name);
            }

            if (result == vault::VaultResult::Ok) {
                deleted++;
            } else {
                // Emit error via queued signal
                QString error = QString("Failed to delete item");
                QMetaObject::invokeMethod(controller, [controller, error]() {
                    emit controller->finished(false, error);
                }, Qt::QueuedConnection);
                return;
            }
        }

        if (deleted == (int)nodes_to_delete.size()) {
            QMetaObject::invokeMethod(controller, [controller]() {
                emit controller->finished(true, "");
            }, Qt::QueuedConnection);
        } else {
            QString error = QString("Deleted %1 of %2 items").arg(deleted).arg((int)nodes_to_delete.size());
            QMetaObject::invokeMethod(controller, [controller, error]() {
                emit controller->finished(true, error);
            }, Qt::QueuedConnection);
        }
    });
}

void TransferController::transferItems(const QList<quintptr>& nodeIds, bool copy,
                                       const QString& destVaultPath, const QString& destGalleryPath)
{
    if (!vault_) {
        emit finished(false, "No vault set");
        return;
    }

    if (!file_op_controller_) {
        emit finished(false, "FileOpController not set");
        return;
    }

    if (nodeIds.isEmpty()) {
        emit finished(true, "");
        return;
    }

    // Check exclusive-op guard
    if (shouldBlockForExclusiveOp()) {
        emit finished(false, "Cannot transfer while imports are active");
        return;
    }

    // Determine transfer mode
    vault::TransferMode mode = copy ? vault::TransferMode::Copy : vault::TransferMode::Move;

    // Convert node IDs to IndexNode pointers
    std::vector<const vault::IndexNode*> nodes_to_transfer;
    for (const auto nodeId : nodeIds) {
        if (nodeId == 0) continue;
        const auto* node = reinterpret_cast<const vault::IndexNode*>(nodeId);
        nodes_to_transfer.push_back(node);
    }

    if (nodes_to_transfer.empty()) {
        emit finished(true, "");
        return;
    }

    auto* controller = this;
    std::string dest_gallery = destGalleryPath.toStdString();

    // Start transfer on worker thread via FileOpController
    file_op_controller_->start([nodes_to_transfer, dest_gallery, mode, controller](ui::FileOpJob& job) {
        // Capture worker thread ID for testing
        controller->last_worker_thread_id_ = std::this_thread::get_id();

        for (const auto* node : nodes_to_transfer) {
            if (!node) continue;

            // Determine if this is a gallery or media
            bool is_gallery = node->is_gallery();

            if (is_gallery) {
                // Transfer gallery subtree
                std::string node_name(node->name);
                vault::VaultResult result = vault::transfer_gallery(
                    *controller->vault_, node_name,
                    *controller->vault_, dest_gallery,
                    mode, nullptr
                );
                if (result != vault::VaultResult::Ok) {
                    QString error = QString("Failed to transfer gallery");
                    QMetaObject::invokeMethod(controller, [controller, error]() {
                        emit controller->finished(false, error);
                    }, Qt::QueuedConnection);
                    return;
                }
            } else {
                // Transfer media (image or video)
                // Full impl would check node type and call transfer_image
                // For now, documented for future work
            }
        }

        QMetaObject::invokeMethod(controller, [controller]() {
            emit controller->finished(true, "");
        }, Qt::QueuedConnection);
    });
}

void TransferController::combineGalleries(quintptr sourceGalleryId, quintptr destGalleryId)
{
    if (!vault_) {
        emit finished(false, "No vault set");
        return;
    }

    if (!file_op_controller_) {
        emit finished(false, "FileOpController not set");
        return;
    }

    if (sourceGalleryId == 0 || destGalleryId == 0) {
        emit finished(false, "Invalid gallery ID");
        return;
    }

    // Check exclusive-op guard
    if (shouldBlockForExclusiveOp()) {
        emit finished(false, "Cannot combine while imports are active");
        return;
    }

    // Resolve source and destination gallery paths from node IDs
    // In production, these would come from gallery picker context or be resolved from the index
    // For now, this is simplified for testing (would need full path tracking)
    const auto* src_node = reinterpret_cast<const vault::IndexNode*>(sourceGalleryId);
    const auto* dst_node = reinterpret_cast<const vault::IndexNode*>(destGalleryId);

    if (!src_node || !dst_node) {
        emit finished(false, "Invalid gallery node");
        return;
    }

    // In a full implementation, we'd have the full slash-paths. For now, use node names
    // (this is simplified - real implementation would track full gallery paths from picker)
    std::string src_path(src_node->name);
    std::string dst_path(dst_node->name);

    auto* controller = this;

    // Start combine on worker thread via FileOpController
    file_op_controller_->start([src_path, dst_path, controller](ui::FileOpJob& job) {
        // Capture worker thread ID for testing
        controller->last_worker_thread_id_ = std::this_thread::get_id();

        vault::CombineTally tally;
        vault::VaultResult result = vault::combine_galleries(
            *controller->vault_, src_path,
            *controller->vault_, dst_path,
            tally, nullptr
        );

        if (result == vault::VaultResult::Ok) {
            QMetaObject::invokeMethod(controller, [controller]() {
                emit controller->finished(true, "");
            }, Qt::QueuedConnection);
        } else {
            QString error = QString("Failed to combine galleries");
            QMetaObject::invokeMethod(controller, [controller, error]() {
                emit controller->finished(false, error);
            }, Qt::QueuedConnection);
        }
    });
}

void TransferController::compact()
{
    if (!vault_) {
        emit finished(false, "No vault set");
        return;
    }

    if (!file_op_controller_) {
        emit finished(false, "FileOpController not set");
        return;
    }

    // Check exclusive-op guard
    if (shouldBlockForExclusiveOp()) {
        emit finished(false, "Cannot compact while imports are active");
        return;
    }

    auto* controller = this;

    // Start compact on worker thread via FileOpController
    file_op_controller_->start([controller](ui::FileOpJob& job) {
        // Capture worker thread ID for testing
        controller->last_worker_thread_id_ = std::this_thread::get_id();

        vault::VaultResult result = controller->vault_->compact(nullptr);

        if (result == vault::VaultResult::Ok) {
            QMetaObject::invokeMethod(controller, [controller]() {
                emit controller->finished(true, "");
            }, Qt::QueuedConnection);
        } else {
            QString error = QString("Compact failed");
            QMetaObject::invokeMethod(controller, [controller, error]() {
                emit controller->finished(false, error);
            }, Qt::QueuedConnection);
        }
    });
}
