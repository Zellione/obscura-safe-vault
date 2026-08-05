#include "test_framework.h"

#include "ui/second_vault.h"

using platform::SecondVaultMode;

TEST(second_vault_starts_empty_with_lock_now_default)
{
    ui::SecondVaultSession s;
    CHECK_FALSE(s.occupied());
    CHECK(s.default_mode() == SecondVaultMode::LockNow);
    CHECK_FALSE(ui::second_vault_status().occupied);
}

TEST(second_vault_adopt_occupies_and_pushes_status)
{
    ui::SecondVaultSession s;
    s.adopt(vault::Vault{}, "/tmp/b.osv", SecondVaultMode::KeepTimed);
    CHECK(s.occupied());
    CHECK_EQ(s.path(), std::string("/tmp/b.osv"));
    CHECK(s.mode() == SecondVaultMode::KeepTimed);
    CHECK_EQ(s.seconds_left(), ui::SecondVaultSession::KEEP_OPEN_SECS);
    const auto st = ui::second_vault_status();
    CHECK(st.occupied);
    CHECK_EQ(st.path, std::string("/tmp/b.osv"));
    s.wipe();
    CHECK_FALSE(ui::second_vault_status().occupied);
}

TEST(second_vault_timed_expires_and_wipes)
{
    ui::SecondVaultSession s;
    s.adopt(vault::Vault{}, "/tmp/b.osv", SecondVaultMode::KeepTimed);
    CHECK_FALSE(s.tick(ui::SecondVaultSession::KEEP_OPEN_SECS - 1.0, /*defer=*/false));
    CHECK(s.occupied());
    CHECK(s.tick(2.0, /*defer=*/false));       // crosses the deadline → expired
    CHECK_FALSE(s.occupied());
}

TEST(second_vault_transfer_completed_slides_the_window)
{
    ui::SecondVaultSession s;
    s.adopt(vault::Vault{}, "/tmp/b.osv", SecondVaultMode::KeepTimed);
    (void)s.tick(ui::SecondVaultSession::KEEP_OPEN_SECS - 5.0, false);
    s.on_transfer_completed();                 // reset to full window
    CHECK_EQ(s.seconds_left(), ui::SecondVaultSession::KEEP_OPEN_SECS);
    CHECK_FALSE(s.tick(ui::SecondVaultSession::KEEP_OPEN_SECS - 1.0, false));
    CHECK(s.occupied());
}

TEST(second_vault_defer_freezes_the_countdown)
{
    ui::SecondVaultSession s;
    s.adopt(vault::Vault{}, "/tmp/b.osv", SecondVaultMode::KeepTimed);
    // A huge deferred dt must not expire the slot NOR consume the window.
    CHECK_FALSE(s.tick(10'000.0, /*defer=*/true));
    CHECK(s.occupied());
    CHECK_EQ(s.seconds_left(), ui::SecondVaultSession::KEEP_OPEN_SECS);
}

TEST(second_vault_session_mode_never_expires)
{
    ui::SecondVaultSession s;
    s.adopt(vault::Vault{}, "/tmp/b.osv", SecondVaultMode::KeepSession);
    CHECK_FALSE(s.tick(10'000'000.0, false));
    CHECK(s.occupied());
    s.wipe();                                  // explicit lock is the only exit
    CHECK_FALSE(s.occupied());
}

TEST(second_vault_adopt_replaces_previous_occupant)
{
    ui::SecondVaultSession s;
    s.adopt(vault::Vault{}, "/tmp/b.osv", SecondVaultMode::KeepSession);
    s.adopt(vault::Vault{}, "/tmp/c.osv", SecondVaultMode::KeepTimed);
    CHECK(s.occupied());
    CHECK_EQ(s.path(), std::string("/tmp/c.osv"));
    CHECK(s.mode() == SecondVaultMode::KeepTimed);
}

TEST(second_vault_take_empties_the_slot_without_wiping)
{
    ui::SecondVaultSession s;
    s.adopt(vault::Vault{}, "/tmp/b.osv", SecondVaultMode::KeepTimed);
    vault::Vault v = s.take();
    (void)v;                                   // promotion path owns it now
    CHECK_FALSE(s.occupied());
    CHECK_FALSE(ui::second_vault_status().occupied);
    CHECK_FALSE(s.tick(10'000.0, false));      // empty slot: inert
}

TEST(second_vault_tick_on_empty_slot_is_inert)
{
    ui::SecondVaultSession s;
    CHECK_FALSE(s.tick(10'000.0, false));
    CHECK_FALSE(s.occupied());
}

TEST(format_keep_open_left_formats_mm_ss)
{
    CHECK_EQ(ui::format_keep_open_left(272.0), std::string("4:32"));
    CHECK_EQ(ui::format_keep_open_left(300.0), std::string("5:00"));
    CHECK_EQ(ui::format_keep_open_left(9.4),   std::string("0:09"));
    CHECK_EQ(ui::format_keep_open_left(-3.0),  std::string("0:00"));
}
