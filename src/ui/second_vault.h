#pragma once

#include <string>

#include "platform/second_vault_pref.h"
#include "vault/vault.h"

namespace ui {

// Read-only snapshot for badge-drawing screens (VaultManager, QuickSwitch, App
// badge). Pushed by SecondVaultSession on every mutation and tick; a plain
// global like media/volume_setting so four screen ctors don't grow a parameter.
struct SecondVaultStatus {
    bool                      occupied = false;
    std::string               path;                                       // vault file path
    platform::SecondVaultMode mode = platform::SecondVaultMode::LockNow;  // of the occupant
    double                    seconds_left = 0.0;                         // KeepTimed only
};
[[nodiscard]] SecondVaultStatus second_vault_status();

// mm:ss for the badge/row labels ("4:32"); clamps negatives to "0:00".
[[nodiscard]] std::string format_keep_open_left(double secs);

class SecondVaultSession {
public:
    static constexpr double KEEP_OPEN_SECS = 5 * 60.0;

    SecondVaultSession();                       // resets the global status
    ~SecondVaultSession();                      // wipe() backstop
    SecondVaultSession(const SecondVaultSession&)            = delete;
    SecondVaultSession& operator=(const SecondVaultSession&) = delete;

    [[nodiscard]] bool occupied() const noexcept;
    [[nodiscard]] const std::string& path() const noexcept;
    [[nodiscard]] vault::Vault& vault() noexcept;            // valid only while occupied
    [[nodiscard]] platform::SecondVaultMode mode() const noexcept;
    [[nodiscard]] double seconds_left() const noexcept;      // KeepTimed; 0 otherwise

    // Take ownership of a just-used destination vault. Keep modes only (LockNow
    // callers simply never call this). Locks any previous occupant first
    // (commit-then-lock: caller only calls this after ITS unlock succeeded).
    void adopt(vault::Vault&& v, std::string path, platform::SecondVaultMode m);

    // A transfer into the occupant completed: sliding reset to KEEP_OPEN_SECS.
    void on_transfer_completed() noexcept;

    // Advance the deadline. `defer` (a job is writing / imports busy) freezes the
    // countdown at its current value — expiry must never wipe under a worker.
    // Returns true when this tick expired and wiped the slot (caller repaints).
    bool tick(double dt, bool defer) noexcept;

    void wipe() noexcept;                       // lock + clear + push status
    [[nodiscard]] vault::Vault take();          // promotion: move handle out, clear slot

    // Session copy of the persisted default (seeded by App at init, updated by
    // the F2 overlay). What the picker's selector starts on.
    [[nodiscard]] platform::SecondVaultMode default_mode() const noexcept;
    void set_default_mode(platform::SecondVaultMode m) noexcept;

private:
    vault::Vault vault_;
    std::string path_;
    platform::SecondVaultMode mode_ = platform::SecondVaultMode::LockNow;
    double seconds_left_ = 0.0;
    bool occupied_ = false;
    platform::SecondVaultMode default_mode_ = platform::SecondVaultMode::LockNow;

    void push_status_() const noexcept;
};

} // namespace ui
