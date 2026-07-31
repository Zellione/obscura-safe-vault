#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>

namespace vault { class Vault; }

// Controller for importing tags from a .txt file list
// Wraps TagListImportAdapter for QML access
class TagListImportController : public QObject {
    Q_OBJECT

public:
    explicit TagListImportController(QObject* parent = nullptr);

    void setVault(vault::Vault* v) noexcept { vault_ = v; }

    // Import tags from file bytes to a gallery node
    // Returns number of tags added, or -1 on error
    // On error, errorMessage() will contain the error text
    Q_INVOKABLE int importTagsFromBytes(const QString& nodePath, const QByteArray& fileBytes);

    // Import tags from a file path to a gallery node
    // Reads the file and imports tags
    Q_INVOKABLE int importTagsFromFile(const QString& nodePath, const QString& filePath);

    // Get the last error message (empty if no error)
    Q_INVOKABLE QString errorMessage() const { return lastError_; }

    // Clear the error message
    Q_INVOKABLE void clearError() { lastError_ = ""; }

signals:
    void importFinished(int count);

private:
    vault::Vault* vault_ = nullptr;
    QString lastError_;

    bool isVaultReady() const;
};
