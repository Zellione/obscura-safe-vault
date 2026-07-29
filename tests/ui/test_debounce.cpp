#include "ui/debounce.h"

#include "test_framework.h"

TEST(debounce_unarmed_never_fires)
{
    ui::Debounce d;
    CHECK(!d.fire(10.0));
}

TEST(debounce_fires_once_after_delay)
{
    ui::Debounce d;   // 0.15 s default
    d.arm();
    CHECK(!d.fire(0.10));
    CHECK(d.fire(0.10));    // 0.20 s accumulated
    CHECK(!d.fire(10.0));   // one-shot
}

TEST(debounce_rearm_restarts_the_clock)
{
    ui::Debounce d;
    d.arm();
    CHECK(!d.fire(0.10));
    d.arm();                 // keystroke during the wait
    CHECK(!d.fire(0.10));    // only 0.10 since the LAST arm
    CHECK(d.fire(0.10));
}

TEST(debounce_cancel_disarms)
{
    ui::Debounce d;
    d.arm(); d.cancel();
    CHECK(!d.fire(10.0));
    CHECK(!d.armed());
}
