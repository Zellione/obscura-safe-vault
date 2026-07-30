#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <filesystem>

// Export controller: wraps ui::export with consent modal + path containment checks
// Security invariant-1 exception: sole feature that deliberately writes plaintext to disk
// Mitigations: consent-modal (default-cancel), selection-only, originalsonly, wipe-after-write
class ExportController : public QObject {
    Q_OBJECT

public:
    explicit ExportController(QObject* parent = nullptr);
    ~ExportController();

    // Start export after user consent
    // destination: folder to export to (must pass containment check)
    // nodeIds: selected node IDs to export (selection-only)
    Q_INVOKABLE void startExport(const QString& destination, const QList<quintptr>& nodeIds);

    // Cancel in-flight export
    Q_INVOKABLE void cancel();

signals:
    // Emitted when export progress updates
    void progressUpdated(int done, int total);

    // Emitted when export completes
    void finished(bool ok, QString error);

private:
    bool validateExportPath(const std::filesystem::path& destDir);
};
