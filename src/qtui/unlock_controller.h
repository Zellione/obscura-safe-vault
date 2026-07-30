#pragma once

#include <QObject>
#include <QUrl>
#include <span>

#include "vault/vault.h"

class SecureTextField;
class SecureImageItem;
class ViewerController;
class PlaybackEngine;

class UnlockController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool unlocked READ unlocked NOTIFY unlockedChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
public:
    explicit UnlockController(QObject* parent = nullptr);

    // Set viewer controller for coordinated shutdown (optional, for async image loading).
    void setViewerController(ViewerController* viewer) noexcept { viewerController_ = viewer; }

    // Set playback engine for coordinated shutdown (optional, for video playback).
    void setPlaybackEngine(PlaybackEngine* playback) noexcept { playbackEngine_ = playback; }

    Q_INVOKABLE bool openVault(const QUrl& fileUrl);
    Q_INVOKABLE void unlock(SecureTextField* field);
    Q_INVOKABLE void unlockWithKeyfile(SecureTextField* passwordField, SecureTextField* keyfileField);
    Q_INVOKABLE void lock();

    // Create a new vault at the given path with the given password and optional keyfile.
    // The vault is created and immediately unlocked on success.
    // Returns true on success, false on error (errorText contains the reason).
    Q_INVOKABLE bool createVault(const QUrl& fileUrl, SecureTextField* passwordField, SecureTextField* confirmField, SecureTextField* keyfileField);

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
    ViewerController* viewerController_ = nullptr;
    PlaybackEngine* playbackEngine_ = nullptr;
};
