#include "ui/unlock_job.h"

#include <cstring>
#include <system_error>
#include <utility>

namespace ui {

namespace {

// Test-only fault-injection for launch() (OSV-AUD-006): forces worker-thread
// creation to fail so the "job cannot launch ⇒ both secrets are wiped" path is
// deterministically tested. Disarmed by default; a single cold branch on the
// launch path (the inject_sync_failure convention). Function-local static keeps
// the flag process-wide without a namespace-scope global (cpp:S5421).
[[nodiscard]] std::atomic_bool& unlock_thread_creation_fail_flag() noexcept
{
    static std::atomic_bool flag{false};
    return flag;
}

}  // namespace

void test_only_force_unlock_thread_failure(bool on) noexcept
{
    unlock_thread_creation_fail_flag().store(on);
}

bool UnlockJob::start_unlock(vault::Vault& v, std::string path,
                             std::span<const uint8_t> password,
                             std::span<const uint8_t> keyfile)
{
    return launch(password, keyfile, [this, &v, path = std::move(path)]() {
        using enum vault::VaultResult;
        vault::VaultResult r = vault::Vault::open(path, v);
        if (r == Ok) r = v.unlock(pw_.span(), keyfile_.span());
        return r;
    });
}

bool UnlockJob::start_create(vault::Vault& v, std::string path,
                             std::span<const uint8_t> password,
                             std::span<const uint8_t> keyfile,
                             const crypto::KdfParams& params)
{
    return launch(password, keyfile, [this, &v, path = std::move(path), params]() {
        return vault::Vault::create(path, pw_.span(), keyfile_.span(), params, v);
    });
}

std::optional<vault::VaultResult> UnlockJob::take_outcome()
{
    if (!active_.load() || !done_.load()) return std::nullopt;
    if (thread_.joinable()) thread_.join();
    active_.store(false);
    return outcome_;
}

bool UnlockJob::launch(std::span<const uint8_t> password, std::span<const uint8_t> keyfile,
                       std::function<vault::VaultResult()> work)
{
    if (active_.load()) return false;   // one unlock at a time

    // Copy the secrets into mlock'd storage on the calling thread, so the
    // caller may wipe its own buffers the moment this returns.
    auto copy_secret = [](crypto::SecureBytes& dst, std::span<const uint8_t> src) {
        if (!dst.resize(src.size())) return false;
        if (!src.empty()) std::memcpy(dst.data(), src.data(), src.size());
        return true;
    };
    if (!copy_secret(pw_, password) || !copy_secret(keyfile_, keyfile)) {
        // OSV-AUD-006: a partially copied secret must never outlive a failed
        // launch — release and wipe both buffers rather than leaving pw_ (or
        // keyfile_) resident behind a job that was never started.
        pw_ = crypto::SecureBytes{};
        keyfile_ = crypto::SecureBytes{};
        return false;
    }

    done_.store(false);
    active_.store(true);
    try {
        if (unlock_thread_creation_fail_flag().load()) {
            throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again),
                                    "test-only: forced thread creation failure");
        }
        thread_ = std::jthread([this, work = std::move(work)]() {
            outcome_ = work();
            // The vault holds the derived key now (or the attempt failed); either
            // way the inputs are no longer needed on any path.
            pw_.wipe();
            keyfile_.wipe();
            done_.store(true);
        });
        return true;
    } catch (...) {
        // The worker thread could not be created (or an allocation in thread
        // machinery failed): the secrets must not stay resident waiting for a
        // worker that will never run.
        pw_ = crypto::SecureBytes{};
        keyfile_ = crypto::SecureBytes{};
        active_.store(false);
        done_.store(true);
        return false;
    }
}

} // namespace ui
