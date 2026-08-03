#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <thread>

#include "crypto/kdf.h"
#include "crypto/secure_mem.h"
#include "vault/vault.h"

namespace ui {

// Runs the vault open/create + Argon2id unlock on a background thread so the
// unlock screen never freezes during key derivation — the KDF is memory-hard
// by design and takes hundreds of milliseconds to seconds depending on the
// machine (create wraps the fresh master key with the same KDF, so both paths
// pay it).
//
// Threading contract: while active(), the worker thread is the ONLY thread
// touching the target vault; the owning screen must not read it until
// take_outcome() returns. The KDF itself is not interruptible, so there is no
// cancel — the destructor joins, bounded by one KDF run. The password/keyfile
// are copied into mlock'd SecureBytes on start and wiped by the worker as
// soon as the vault call returns.
class UnlockJob {
public:
    UnlockJob() = default;

    UnlockJob(const UnlockJob&)            = delete;
    UnlockJob& operator=(const UnlockJob&) = delete;

    // Open + unlock an existing vault at `path` into `v`.
    bool start_unlock(vault::Vault& v, std::string path,
                      std::span<const uint8_t> password,
                      std::span<const uint8_t> keyfile);

    // Create a new vault at `path` (leaves `v` unlocked on success).
    bool start_create(vault::Vault& v, std::string path,
                      std::span<const uint8_t> password,
                      std::span<const uint8_t> keyfile,
                      const crypto::KdfParams& params);

    // True from a successful start_*() until take_outcome() collected the result.
    [[nodiscard]] bool active() const noexcept { return active_.load(); }

    // If the worker has finished, join it and hand back the result exactly
    // once; nullopt while still running or when there is nothing to collect.
    [[nodiscard]] std::optional<vault::VaultResult> take_outcome();

private:
    // Copy the secrets, then spawn `work` on the worker thread. Returns false
    // if a job is already in flight.
    bool launch(std::span<const uint8_t> password, std::span<const uint8_t> keyfile,
                std::function<vault::VaultResult()> work);

    crypto::SecureBytes pw_;        // wiped by the worker after use
    crypto::SecureBytes keyfile_;   // "
    std::atomic<bool>   active_{false};
    std::atomic<bool>   done_{false};
    vault::VaultResult  outcome_ = vault::VaultResult::Ok;
    // Declared last: destructs (joins) first, before the secret members above.
    std::jthread        thread_;
};

} // namespace ui
