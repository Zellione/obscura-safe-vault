#include "ui/unlock_job.h"

#include <cstring>
#include <utility>

namespace ui {

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
    if (!active_.load() || !done_.load(std::memory_order_acquire)) return std::nullopt;
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
    if (!copy_secret(pw_, password) || !copy_secret(keyfile_, keyfile)) return false;

    done_.store(false);
    active_.store(true);
    thread_ = std::jthread([this, work = std::move(work)]() {
        outcome_ = work();
        // The vault holds the derived key now (or the attempt failed); either
        // way the inputs are no longer needed on any path.
        pw_.wipe();
        keyfile_.wipe();
        done_.store(true, std::memory_order_release);
    });
    return true;
}

} // namespace ui
