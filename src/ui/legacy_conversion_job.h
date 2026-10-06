#pragma once

#include "crypto/secure_mem.h"
#include "vault/legacy_converter.h"

#include <atomic>
#include <filesystem>
#include <optional>
#include <span>
#include <thread>

namespace ui {

class LegacyConversionJob {
public:
    LegacyConversionJob() = default;
    ~LegacyConversionJob();

    LegacyConversionJob(const LegacyConversionJob&) = delete;
    LegacyConversionJob& operator=(const LegacyConversionJob&) = delete;

    [[nodiscard]] bool start(vault::Vault& source, std::filesystem::path destination,
                             std::span<const uint8_t> password, std::span<const uint8_t> keyfile,
                             const crypto::KdfParams& kdf = crypto::DEFAULT_KDF_PARAMS);
    void cancel() noexcept;
    [[nodiscard]] bool active() const noexcept
    {
        return active_.load();
    }
    [[nodiscard]] const vault::OpProgress& progress() const noexcept
    {
        return progress_;
    }
    [[nodiscard]] std::optional<vault::LegacyConversionReport> take_outcome();

private:
    crypto::SecureBytes password_;
    crypto::SecureBytes keyfile_;
    std::atomic_bool cancel_{false};
    std::atomic_bool active_{false};
    std::atomic_bool done_{false};
    vault::OpProgress progress_;
    vault::LegacyConversionReport outcome_;
    std::jthread thread_;
};

}  // namespace ui
