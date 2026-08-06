#include "ui/second_vault.h"

#include <format>

namespace ui {

namespace {
// File-local global: the read-only snapshot pushed by every mutator and tick.
// Single-threaded UI state, main thread only.
SecondVaultStatus& global_status()
{
    static SecondVaultStatus s;
    return s;
}
}  // namespace

SecondVaultStatus second_vault_status()
{
    return global_status();
}

std::string format_keep_open_left(double secs)
{
    secs = std::max(0.0, secs);
    auto t = static_cast<int>(secs);
    return std::format("{}:{:02}", t / 60, t % 60);
}

SecondVaultSession::SecondVaultSession()
{
    global_status() = SecondVaultStatus{};
}

SecondVaultSession::~SecondVaultSession()
{
    wipe();
}

bool SecondVaultSession::occupied() const noexcept
{
    return occupied_;
}

const std::string& SecondVaultSession::path() const noexcept
{
    return path_;
}

vault::Vault& SecondVaultSession::vault() noexcept
{
    return vault_;
}

platform::SecondVaultMode SecondVaultSession::mode() const noexcept
{
    return mode_;
}

double SecondVaultSession::seconds_left() const noexcept
{
    return seconds_left_;
}

void SecondVaultSession::adopt(vault::Vault&& v, std::string path, platform::SecondVaultMode m)
{
    wipe();  // lock any previous occupant first
    vault_ = std::move(v);
    path_ = std::move(path);
    mode_ = m;
    seconds_left_ = KEEP_OPEN_SECS;
    occupied_ = true;
    push_status_();
}

void SecondVaultSession::on_transfer_completed() noexcept
{
    if (occupied_) {
        seconds_left_ = KEEP_OPEN_SECS;
        push_status_();
    }
}

bool SecondVaultSession::tick(double dt, bool defer) noexcept
{
    if (!occupied_ || mode_ != platform::SecondVaultMode::KeepTimed) {
        return false;
    }
    if (defer) {
        return false;
    }
    seconds_left_ -= dt;
    if (seconds_left_ > 0) {
        push_status_();
        return false;
    }
    wipe();
    return true;
}

void SecondVaultSession::wipe() noexcept
{
    if (vault_.is_unlocked()) {
        vault_.lock();
    }
    occupied_ = false;
    path_.clear();
    seconds_left_ = 0.0;
    mode_ = platform::SecondVaultMode::LockNow;
    push_status_();
}

vault::Vault SecondVaultSession::take()
{
    occupied_ = false;
    path_.clear();
    push_status_();
    return std::move(vault_);
}

platform::SecondVaultMode SecondVaultSession::default_mode() const noexcept
{
    return default_mode_;
}

void SecondVaultSession::set_default_mode(platform::SecondVaultMode m) noexcept
{
    default_mode_ = m;
}

void SecondVaultSession::push_status_() noexcept
{
    global_status().occupied = occupied_;
    global_status().path = path_;
    global_status().mode = mode_;
    global_status().seconds_left = seconds_left_;
}

} // namespace ui
