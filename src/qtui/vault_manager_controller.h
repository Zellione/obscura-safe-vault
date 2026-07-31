#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <memory>

class VaultListModel;

// Manages vault registry operations: add/remove from known vaults, open from
// file dialog. Used by VaultManagerScreen to populate the vault list and handle
// user interactions.
class VaultManagerController : public QObject {
    Q_OBJECT
    // Model is created in the constructor and lives as long as the controller.
    Q_PROPERTY(VaultListModel* vaultListModel READ vaultListModel CONSTANT)
public:
    explicit VaultManagerController(QObject* parent = nullptr);

    // Reuse existing VaultListModel for the list of known vaults.
    VaultListModel* vaultListModel() const noexcept { return vaultListModel_.get(); }

    // Remove a vault from the registry (does NOT delete the file).
    // Returns true on success.
    Q_INVOKABLE bool removeVaultFromRegistry(int row);

    // Add a vault file URL to the registry (move-to-front if already there).
    // Returns true on success.
    Q_INVOKABLE bool addVaultToRegistry(const QUrl& fileUrl);

    // Refresh the vault list model.
    Q_INVOKABLE void refreshVaultList();

signals:
    // Emitted when a vault is selected from the open dialog (or registry).
    // Contains a file:// URL to be passed to UnlockController::openVault().
    void openedVaultPath(const QUrl& fileUrl);

private:
    std::unique_ptr<VaultListModel> vaultListModel_;
};
