#pragma once

#include <QObject>
#include <QUrl>
#include <span>

#include "vault/vault.h"

class SecureTextField;
class SecureImageItem;

class UnlockController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool unlocked READ unlocked NOTIFY unlockedChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
public:
    Q_INVOKABLE bool openVault(const QUrl& fileUrl);
    Q_INVOKABLE void unlock(SecureTextField* field);
    Q_INVOKABLE void lock();

    // TEMPORARY (Task 5 proof) — removed in Task 6
    Q_INVOKABLE void loadFirstImage(SecureImageItem* item);

    // Test-only helper for selftest (not exposed to QML)
    bool unlockWithPassword(const std::span<const uint8_t>& password);

    [[nodiscard]] bool unlocked() const { return vault_.is_unlocked(); }
    [[nodiscard]] QString errorText() const { return error_; }
    [[nodiscard]] vault::Vault& vault() noexcept { return vault_; }

signals:
    void unlockedChanged();
    void errorTextChanged();

private:
    void setError(const QString& e);
    vault::Vault vault_;
    QString      error_;
};
