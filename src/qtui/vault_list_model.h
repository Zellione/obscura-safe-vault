#pragma once

#include <QAbstractListModel>
#include <QUrl>
#include <filesystem>
#include <vector>

// Known-vault list for the unlock screen: entries from the SDL app's
// vaults.list registry (most-recent-first) plus any *.osv files found in the
// per-user data folder (platform::config_dir()). The data folder lives under a
// hidden directory (~/.local/share/... on Linux), which native file dialogs
// don't show — this model makes those vaults pickable without the dialog.
//
// Read-only: the model never writes the registry. It stores no secrets — only
// filesystem paths of vault files (the same strings vaults.list holds). Vault
// CONTENT and node names are never touched here.
class VaultListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,  // filename, e.g. "vault.osv"
        DirRole,                      // parent directory (display hint)
    };

    explicit VaultListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // Re-read registry + data-folder scan.
    Q_INVOKABLE void refresh();

    // file:// URL for the row, for UnlockController::openVault. Empty on bad row.
    Q_INVOKABLE QUrl fileUrlAt(int row) const;

signals:
    void countChanged();

private:
    std::vector<std::filesystem::path> vaults_;
};
