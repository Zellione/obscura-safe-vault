#include "ui/legacy_conversion_job.h"

#include "platform/path_utf8.h"
#include "platform/safe_print.h"
#include "vault/vault.h"

#include <cstring>
#include <new>
#include <system_error>
#include <utility>

namespace ui {

void LegacyConversionJob::run(vault::Vault& source, const std::filesystem::path& destination,
                              const crypto::KdfParams& kdf) noexcept
{
            const vault::LegacyConversionRequest request{
                destination, password_.as_span(), keyfile_.as_span(), kdf, &cancel_, &progress_};
            outcome_ = vault::convert_legacy_vault(source, request);
            if (outcome_.status == vault::LegacyConversionStatus::Ok) {
                vault::Vault converted;
                const auto path = platform::path_to_utf8(destination);
                const auto opened = vault::Vault::open(path, converted);
        const auto unlocked = opened == vault::VaultResult::Ok
                        ? converted.unlock(password_.as_span(), keyfile_.as_span())
                        : opened;
                if (unlocked == vault::VaultResult::Ok) {
                    source.lock();
                    source = std::move(converted);
                } else {
                    outcome_.status = vault::LegacyConversionStatus::VerificationFailed;
                }
            }
            password_.wipe();
            keyfile_.wipe();
            done_.store(true);
    }

void LegacyConversionJob::fail_start() noexcept
{
    password_ = {};
    keyfile_ = {};
    active_.store(false);
    done_.store(true);
}

LegacyConversionJob::~LegacyConversionJob()
{
    cancel();
    if (thread_.joinable()) thread_.join();
}

bool LegacyConversionJob::start(vault::Vault& source, std::filesystem::path destination,
                                std::span<const uint8_t> password, std::span<const uint8_t> keyfile,
                                const crypto::KdfParams& kdf)
{
    if (active_.load() || !source.is_unlocked() || !vault::vault_is_read_only(source)) return false;
    if (!password_.assign(password) || !keyfile_.assign(keyfile)) {
        password_ = {};
        keyfile_ = {};
        return false;
    }
    cancel_.store(false);
    done_.store(false);
    active_.store(true);
    try {
        thread_ = std::jthread([this, &source, destination = std::move(destination), kdf] {
            run(source, destination, kdf);
        });
        return true;
    } catch (const std::system_error& error) {
        platform::safe_println(stderr, "[Converter] worker start failed: {}", error.what());
    } catch (const std::bad_alloc& error) {
        platform::safe_println(stderr, "[Converter] worker allocation failed: {}", error.what());
    }
    fail_start();
    return false;
}

void LegacyConversionJob::cancel() noexcept
{
    cancel_.store(true);
}

std::optional<vault::LegacyConversionReport> LegacyConversionJob::take_outcome()
{
    if (!active_.load() || !done_.load()) return std::nullopt;
    if (thread_.joinable()) thread_.join();
    active_.store(false);
    return outcome_;
}

}  // namespace ui
