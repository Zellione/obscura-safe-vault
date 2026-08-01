#include "unlock_controller.h"

#include <QUrl>
#include <vector>

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

void UnlockController::setCurrentVaultPath(const QUrl& path)
{
    if (currentVaultPath_ != path) {
        currentVaultPath_ = path;
        emit currentVaultPathChanged();
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
        setCurrentVaultPath(fileUrl);
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
        field->clearSecret();          // wipe on all exit paths
        setError(r == vault::VaultResult::AuthFailed
                     ? QStringLiteral("Wrong password")
                     : QStringLiteral("Unlock failed"));
    }
}

void UnlockController::unlockWithKeyfile(SecureTextField* passwordField, const QUrl& keyfileUrl)
{
    if (passwordField == nullptr) return;

    // Get password bytes
    const auto pw_view = passwordField->model().text_view();
    const std::span<const uint8_t> pw_span{reinterpret_cast<const uint8_t*>(pw_view.data()), pw_view.size()};

    // Read keyfile bytes from disk
    const auto keyfilePath = keyfileUrl.toLocalFile().toStdString();
    const auto keyfilePathOpt = platform::normalize_user_path(keyfilePath);
    if (!keyfilePathOpt.has_value()) {
        setError(QStringLiteral("Invalid keyfile path"));
        passwordField->clearSecret();
        return;
    }

    // Read the keyfile through platform::read_file rather than a local
    // fopen/fread loop: it sizes the file first and does a single allocation +
    // single read, precisely so key material isn't strewn across freed heap
    // blocks by a chunk-growing vector (see its comment in platform/paths.cpp).
    // It also spells the path narrow via path::string(), which is what makes
    // this compile on Windows at all — path::value_type is wchar_t there, so
    // passing path::c_str() straight to fopen() is a type error.
    auto keyfileRead = platform::read_file(keyfilePathOpt.value());
    if (!keyfileRead.has_value()) {
        setError(QStringLiteral("Cannot read keyfile"));
        passwordField->clearSecret();
        return;
    }
    std::vector<uint8_t> keyfile_bytes = std::move(keyfileRead.value());

    // Unlock with both password and keyfile
    const std::span<const uint8_t> keyfile_span(keyfile_bytes.data(), keyfile_bytes.size());
    const auto r = vault_.unlock(pw_span, keyfile_span);

    // Wipe keyfile bytes and password on all exit paths
    if (!keyfile_bytes.empty()) {
        crypto_wipe(keyfile_bytes.data(), keyfile_bytes.size());
    }

    if (r == vault::VaultResult::Ok) {
        passwordField->clearSecret();
        setError({});
        emit unlockedChanged();
    } else {
        passwordField->clearSecret();
        setError(r == vault::VaultResult::AuthFailed
                     ? QStringLiteral("Wrong password or keyfile")
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

    setCurrentVaultPath({});
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
                                    SecureTextField* confirmField, const QUrl& keyfileUrl)
{
    if (passwordField == nullptr || confirmField == nullptr) {
        setError(QStringLiteral("Invalid arguments"));
        return false;
    }

    // Validate the vault path
    const auto pathOpt = platform::normalize_user_path(fileUrl.toLocalFile().toStdString());
    if (!pathOpt.has_value()) {
        setError(QStringLiteral("Invalid path"));
        passwordField->clearSecret();
        confirmField->clearSecret();
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

    // Read optional keyfile bytes from disk (empty if no keyfile provided)
    std::vector<uint8_t> keyfile_bytes;
    if (!keyfileUrl.isEmpty()) {
        const auto keyfilePath = keyfileUrl.toLocalFile().toStdString();
        const auto keyfilePathOpt = platform::normalize_user_path(keyfilePath);
        if (!keyfilePathOpt.has_value()) {
            setError(QStringLiteral("Invalid keyfile path"));
            passwordField->clearSecret();
            confirmField->clearSecret();
            return false;
        }

        // Same single-allocation read as the unlock path above.
        auto keyfileRead = platform::read_file(keyfilePathOpt.value());
        if (!keyfileRead.has_value()) {
            setError(QStringLiteral("Cannot read keyfile"));
            passwordField->clearSecret();
            confirmField->clearSecret();
            return false;
        }
        keyfile_bytes = std::move(keyfileRead.value());
    }

    // Create the vault with default KDF params
    vault::Vault newVault;
    const auto result = vault::Vault::create(
        pathOpt.value().string(),
        pw_span,
        std::span<const uint8_t>(keyfile_bytes.data(), keyfile_bytes.size()),
        crypto::DEFAULT_KDF_PARAMS,
        newVault);

    // Wipe password fields and keyfile immediately
    passwordField->clearSecret();
    confirmField->clearSecret();
    if (!keyfile_bytes.empty()) {
        crypto_wipe(keyfile_bytes.data(), keyfile_bytes.size());
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
    setCurrentVaultPath(fileUrl);
    emit unlockedChanged();
    return true;
}

