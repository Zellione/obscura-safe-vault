#include "test_framework.h"

#include <algorithm>

#include "ui/waste_threshold.h"

TEST(should_display_waste_zero_always_false)
{
    CHECK_FALSE(ui::should_display_waste(0, 1000000000));
    CHECK_FALSE(ui::should_display_waste(0, 0));
}

TEST(should_display_waste_at_50mb_absolute_threshold)
{
    // With a small vault where 50 MiB is the actual threshold.
    const uint64_t vault_size = 100 * 1024 * 1024;  // 100 MiB
    const uint64_t fifty_mb = 50 * 1024 * 1024;     // 50 MiB
    // For a 100 MiB vault: max(50 MiB, 10%) = max(50 MiB, 10 MiB) = 50 MiB

    CHECK_FALSE(ui::should_display_waste(fifty_mb - 1, vault_size));  // just below
    CHECK_TRUE(ui::should_display_waste(fifty_mb, vault_size));       // exactly at
    CHECK_TRUE(ui::should_display_waste(fifty_mb + 1, vault_size));   // just above
}

TEST(should_display_waste_at_10percent_relative_threshold)
{
    const uint64_t file_size = 1000 * 1024 * 1024;  // 1 GiB
    const uint64_t fifty_mb = 50 * 1024 * 1024;     // 50 MiB
    const uint64_t ten_percent = file_size / 10;    // 100 MiB
    // For a 1 GiB file, max(50 MiB, 10%) = max(50 MiB, 100 MiB) = 100 MiB
    const uint64_t threshold = std::max(fifty_mb, ten_percent);

    CHECK_FALSE(ui::should_display_waste(threshold - 1, file_size));  // just below
    CHECK_TRUE(ui::should_display_waste(threshold, file_size));       // exactly at
    CHECK_TRUE(ui::should_display_waste(threshold + 1, file_size));   // just above
}

TEST(should_display_waste_small_vault_uses_relative_threshold)
{
    const uint64_t small_vault = 600 * 1024 * 1024;   // 600 MiB
    const uint64_t ten_percent = small_vault / 10;    // 60 MiB
    const uint64_t fifty_mb = 50 * 1024 * 1024;

    // For a 600 MiB vault: max(50 MiB, 60 MiB) = 60 MiB
    // Below 10%: not shown
    CHECK_FALSE(ui::should_display_waste(fifty_mb, small_vault));  // 50 MiB < 60 MiB

    // At 10%: shown
    CHECK_TRUE(ui::should_display_waste(ten_percent, small_vault));  // 60 MiB >= 60 MiB

    // Above 10% of vault: still shown
    CHECK_TRUE(ui::should_display_waste(ten_percent + 1, small_vault));
}

TEST(should_display_waste_zero_vault_size_uses_absolute_only)
{
    const uint64_t fifty_mb = 50 * 1024 * 1024;

    CHECK_FALSE(ui::should_display_waste(fifty_mb - 1, 0));  // vault size unknown (locked?)
    CHECK_TRUE(ui::should_display_waste(fifty_mb, 0));       // still use 50 MiB absolute
}

TEST(should_hint_cancelled_import_waste_at_1mb_threshold)
{
    const uint64_t one_mb = 1024 * 1024;

    CHECK_FALSE(ui::should_hint_cancelled_import_waste(0));
    CHECK_FALSE(ui::should_hint_cancelled_import_waste(one_mb - 1));
    CHECK_TRUE(ui::should_hint_cancelled_import_waste(one_mb));
    CHECK_TRUE(ui::should_hint_cancelled_import_waste(one_mb + 1));
}

TEST(should_hint_cancelled_import_waste_large_waste)
{
    const uint64_t large_waste = 100 * 1024 * 1024;
    CHECK_TRUE(ui::should_hint_cancelled_import_waste(large_waste));
}
