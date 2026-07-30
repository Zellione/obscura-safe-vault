#include "unlock_controller.h"

#include <QUrl>

#include "crypto/secure_mem.h"
#include "crypto/kdf.h"
#include "image/decode.h"
#include "platform/paths.h"
#include "platform/vault_registry.h"
#include "secure_image_item.h"
#include "secure_text_field.h"
#include "pixel_buffer.h"
#include "thumb_cache.h"
#include "viewer_controller.h"
#include "playback_engine.h"
#include "ui/unlock_logic.h"

UnlockController::UnlockController(QObject* parent)
    : QObject(parent)
{
}

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
    // CRITICAL: Drain viewer, playback, and thumbnail workers before locking vault.
    // Drain-before-lock ordering ensures all workers finish before vault wipes the index tree.

    // Stop video playback (worker thread drains on stop())
    if (playbackEngine_) {
        playbackEngine_->stop();
    }

    // Drain viewer controller (async full-image loads)
    if (viewerController_) {
        viewerController_->shutdownAndDrain();
    }

    // Drain thumbnail cache
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

bool UnlockController::createVault(const QUrl& fileUrl, SecureTextField* passwordField,
                                    SecureTextField* confirmField, SecureTextField* keyfileField)
{
    if (passwordField == nullptr || confirmField == nullptr) {
        setError(QStringLiteral("Invalid arguments"));
        return false;
    }

    // Validate the path
    const auto pathOpt = platform::normalize_user_path(fileUrl.toLocalFile().toStdString());
    if (!pathOpt.has_value()) {
        setError(QStringLiteral("Invalid path"));
        return false;
    }

    // Get password and confirm bytes
    const auto pw_view = passwordField->model().text_view();
    const auto confirm_view = confirmField->model().text_view();
    const std::span<const uint8_t> pw_span{reinterpret_cast<const uint8_t*>(pw_view.data()), pw_view.size()};
    const std::span<const uint8_t> confirm_span{reinterpret_cast<const uint8_t*>(confirm_view.data()), confirm_view.size()};

    // Validate the submission (check password matches confirm, strength check)
    const auto decision = ui::decide_submit(true, pw_span, confirm_span);
    if (decision.action != ui::SubmitAction::Create) {
        setError(QString::fromUtf8(decision.error));
        passwordField->clearSecret();
        confirmField->clearSecret();
        return false;
    }

    // Get optional keyfile bytes
    std::vector<uint8_t> keyfile_data;
    if (keyfileField != nullptr) {
        const auto keyfile_view = keyfileField->model().text_view();
        // For now, keyfile is treated as text (binary files come in Task 1.3)
        keyfile_data.assign(
            reinterpret_cast<const uint8_t*>(keyfile_view.data()),
            reinterpret_cast<const uint8_t*>(keyfile_view.data()) + keyfile_view.size());
    }

    // Create the vault with default KDF params
    vault::Vault newVault;
    const auto result = vault::Vault::create(
        pathOpt.value().string(),
        pw_span,
        std::span<const uint8_t>(keyfile_data.data(), keyfile_data.size()),
        crypto::DEFAULT_KDF_PARAMS,
        newVault);

    // Wipe password fields immediately
    passwordField->clearSecret();
    confirmField->clearSecret();
    if (keyfileField) {
        keyfileField->clearSecret();
    }

    if (result != vault::VaultResult::Ok) {
        setError(result == vault::VaultResult::IoError        ? QStringLiteral("Failed to create file")
                 : result == vault::VaultResult::CryptoError  ? QStringLiteral("Crypto error")
                                                              : QStringLiteral("Create failed"));
        return false;
    }

    // Move the newly created vault to the managed vault_
    vault_ = std::move(newVault);

    // Add to registry
    const auto registry = platform::VaultRegistry::default_location();
    registry.add(pathOpt.value());

    setError({});
    emit unlockedChanged();
    return true;
}

