#include "unlock_controller.h"

#include <QUrl>

#include "platform/paths.h"
#include "secure_text_field.h"

void UnlockController::setError(const QString& e)
{
    if (error_ != e) {
        error_ = e;
        emit errorTextChanged();
    }
}

bool UnlockController::openVault(const QUrl& fileUrl)
{
    const auto pathOpt = platform::normalize_user_path(fileUrl.toLocalFile().toStdString());
    if (!pathOpt.has_value()) {
        setError(QStringLiteral("Invalid path"));
        return false;
    }
    const auto result = vault::Vault::open(pathOpt.value().string(), vault_);
    if (result == vault::VaultResult::Ok) {
        setError({});
        return true;
    }
    setError(result == vault::VaultResult::IoError        ? QStringLiteral("Failed to open file")
             : result == vault::VaultResult::BadFormat    ? QStringLiteral("Invalid vault file")
             : result == vault::VaultResult::CryptoError  ? QStringLiteral("Crypto error")
                                                          : QStringLiteral("Open failed"));
    return false;
}

void UnlockController::unlock(SecureTextField* field)
{
    if (field == nullptr) return;
    const std::string_view pw = field->model().text_view();   // view over mlock'd bytes
    const std::span<const uint8_t> span{reinterpret_cast<const uint8_t*>(pw.data()), pw.size()};
    const auto r = vault_.unlock(span, {});
    if (r == vault::VaultResult::Ok) {
        field->clearSecret();          // wipe immediately after successful KDF
        setError({});
        emit unlockedChanged();
    } else {
        setError(r == vault::VaultResult::AuthFailed
                     ? QStringLiteral("Wrong password")
                     : QStringLiteral("Unlock failed"));
    }
}

void UnlockController::lock()
{
    vault_.lock();
    emit unlockedChanged();
}
