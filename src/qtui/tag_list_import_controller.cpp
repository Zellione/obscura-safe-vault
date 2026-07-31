#include "tag_list_import_controller.h"
#include "tag_list_import_adapter.h"
#include <vault/vault.h>
#include <platform/paths.h>
#include <QFile>
#include <QUrl>

TagListImportController::TagListImportController(QObject* parent)
    : QObject(parent)
{
}

bool TagListImportController::isVaultReady() const
{
    return vault_ && vault_->is_unlocked();
}

int TagListImportController::importTagsFromBytes(const QString& nodePath, const QByteArray& fileBytes)
{
    if (!isVaultReady()) {
        lastError_ = "Vault not unlocked";
        emit importFinished(-1);
        return -1;
    }

    TagListImportAdapter adapter(vault_);
    int result = adapter.importTagsFromBytes(nodePath, fileBytes);

    if (result < 0) {
        lastError_ = "Failed to import tags";
    } else {
        lastError_ = "";
    }

    emit importFinished(result);
    return result;
}

int TagListImportController::importTagsFromFile(const QString& nodePath, const QString& filePath)
{
    if (!isVaultReady()) {
        emit importFinished(-1);
        return -1;
    }

    // Convert file:// URL to path if needed
    QString actualPath = filePath;
    if (filePath.startsWith("file://")) {
        actualPath = QUrl(filePath).toLocalFile();
    }

    // SECURITY: Normalize user-supplied path per CLAUDE.md invariant
    // "Externally-supplied paths go through platform::normalize_user_path before fopen"
    auto normalized = platform::normalize_user_path(actualPath.toStdString());
    if (!normalized) {
        lastError_ = "Invalid file path";
        emit importFinished(-1);
        return -1;
    }

    // Read file
    QFile file(QString::fromStdString(normalized->string()));
    if (!file.open(QIODevice::ReadOnly)) {
        lastError_ = "Failed to open file";
        emit importFinished(-1);
        return -1;
    }

    QByteArray fileBytes = file.readAll();
    file.close();

    // Use the byte-based import
    return importTagsFromBytes(nodePath, fileBytes);
}
