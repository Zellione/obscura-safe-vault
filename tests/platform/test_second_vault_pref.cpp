#include "test_framework.h"

#include <filesystem>

#include "platform/second_vault_pref.h"

namespace {
std::filesystem::path temp_pref_file(const char* name)
{
    auto p = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(p);
    return p;
}
} // namespace

TEST(second_vault_pref_missing_file_loads_lock_now)
{
    const platform::SecondVaultPref pref(temp_pref_file("osv_svp_missing.conf"));
    CHECK(pref.load() == platform::SecondVaultMode::LockNow);
}

TEST(second_vault_pref_round_trips_each_mode)
{
    const auto file = temp_pref_file("osv_svp_rt.conf");
    const platform::SecondVaultPref pref(file);
    for (auto m : {platform::SecondVaultMode::LockNow, platform::SecondVaultMode::KeepTimed,
                   platform::SecondVaultMode::KeepSession}) {
        CHECK(pref.save(m));
        CHECK(pref.load() == m);
    }
    std::filesystem::remove(file);
}

TEST(second_vault_pref_garbage_content_loads_lock_now)
{
    const auto file = temp_pref_file("osv_svp_bad.conf");
    {
        auto* fp = std::fopen(file.string().c_str(), "wb");
        std::fputs("definitely-not-a-mode\n", fp);
        std::fclose(fp);
    }
    const platform::SecondVaultPref pref(file);
    CHECK(pref.load() == platform::SecondVaultMode::LockNow);
    std::filesystem::remove(file);
}

TEST(second_vault_pref_empty_pref_saves_nothing)
{
    const platform::SecondVaultPref pref;   // default-constructed: no backing file
    CHECK_FALSE(pref.save(platform::SecondVaultMode::KeepTimed));
}
