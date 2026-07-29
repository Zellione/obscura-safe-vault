#include "unlock_controller.h"

#include <QUrl>

#include "crypto/secure_mem.h"
#include "image/decode.h"
#include "platform/paths.h"
#include "secure_image_item.h"
#include "secure_text_field.h"
#include "pixel_buffer.h"
#include "thumb_cache.h"

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
    // CRITICAL: Drain thumbnail cache before locking vault.
    // Drain-before-lock ordering ensures workers finish before vault wipes the index tree.
    auto cache = ThumbCache::instance();
    if (cache) {
        cache->shutdownAndDrain();
    }

    vault_.lock();

    // Clear cache after vault is locked (vault tree is now freed)
    if (cache) {
        cache->clearAll();
    }

    emit unlockedChanged();
}

// Test-only helper for selftest: unlock with explicit password bytes
bool UnlockController::unlockWithPassword(const std::span<const uint8_t>& password)
{
    const auto r = vault_.unlock(password, {});
    if (r == vault::VaultResult::Ok) {
        setError({});
        emit unlockedChanged();
        return true;
    }
    setError(r == vault::VaultResult::AuthFailed
                 ? QStringLiteral("Wrong password")
                 : QStringLiteral("Unlock failed"));
    return false;
}

