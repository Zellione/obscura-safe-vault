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
    Q_PROPERTY(QUrl currentVaultPath READ currentVaultPath NOTIFY currentVaultPathChanged)
    // Where a new vault goes when the user doesn't pick a location. CONSTANT:
    // it derives from the per-user config dir, which can't change while running.
    Q_PROPERTY(QUrl defaultVaultPath READ defaultVaultPath CONSTANT)
public:
    explicit UnlockController(QObject* parent = nullptr);

    // Set viewer controller for coordinated shutdown (optional, for async image loading).
    void setViewerController(ViewerController* viewer) noexcept { viewerController_ = viewer; }

    // Set playback engine for coordinated shutdown (optional, for video playback).
    void setPlaybackEngine(PlaybackEngine* playback) noexcept { playbackEngine_ = playback; }

    Q_INVOKABLE bool openVault(const QUrl& fileUrl);
    Q_INVOKABLE void unlock(SecureTextField* field);
    Q_INVOKABLE void unlockWithKeyfile(SecureTextField* passwordField, const QUrl& keyfileUrl);
    Q_INVOKABLE void lock();

    // Create a new vault at the given path with the given password and optional keyfile.
    // keyfileUrl is empty if no keyfile is used. Returns true on success.
    Q_INVOKABLE bool createVault(const QUrl& fileUrl, SecureTextField* passwordField, SecureTextField* confirmField, const QUrl& keyfileUrl);

    // Test-only helper for selftest (not exposed to QML)
    bool unlockWithPassword(const std::span<const uint8_t>& password);

    [[nodiscard]] bool unlocked() const { return vault_.is_unlocked(); }
    [[nodiscard]] QString errorText() const { return error_; }
    [[nodiscard]] QUrl currentVaultPath() const { return currentVaultPath_; }
    // platform::default_vault_path() — config_dir()/vault.osv, the same place
    // the SDL app puts a default vault, so both UIs agree on one location.
    [[nodiscard]] QUrl defaultVaultPath() const;
    [[nodiscard]] vault::Vault& vault() noexcept { return vault_; }

signals:
    void unlockedChanged();
    void errorTextChanged();
    void currentVaultPathChanged();

private:
    void setError(const QString& e);
    void setCurrentVaultPath(const QUrl& path);
    vault::Vault vault_;
    QString      error_;
    QUrl         currentVaultPath_;
    ViewerController* viewerController_ = nullptr;
    PlaybackEngine* playbackEngine_ = nullptr;
};
