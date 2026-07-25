// Phase 53: the five limits bounding recursive archive extraction.
//
// A nested archive is untrusted input and recursion turns a small file into
// unbounded work. Each limit fails only the offending BRANCH — one hostile
// archive buried in a legitimate set must not destroy the rest of the import.

#include "test_framework.h"
#include "ui/recursive_import.h"

using ui::RecursionBudget;
using ui::RecursionLimits;
using ui::RecursionVerdict;

namespace {

// Generous defaults so each test isolates the ONE limit it is about.
RecursionLimits wide()
{
    return RecursionLimits{.max_depth           = 100,
                           .max_total_expanded  = 1ULL << 40,
                           .max_live_bytes      = 1ULL << 40,
                           .max_nested_archives = 100000,
                           .max_expansion_ratio = 1000000};
}

} // namespace

TEST(recursion_budget_allows_a_normal_descent)
{
    const RecursionBudget b(wide(), 1000);
    CHECK(b.may_descend(1, 500) == RecursionVerdict::Allow);
}

TEST(recursion_budget_stops_at_max_depth)
{
    RecursionLimits lim = wide();
    lim.max_depth       = 3;
    const RecursionBudget b(lim, 1000);

    CHECK(b.may_descend(2, 10) == RecursionVerdict::Allow);
    CHECK(b.may_descend(3, 10) == RecursionVerdict::DepthExceeded);
    CHECK(b.may_descend(9, 10) == RecursionVerdict::DepthExceeded);
}

TEST(recursion_budget_stops_on_total_expanded_bytes)
{
    RecursionLimits lim       = wide();
    lim.max_total_expanded    = 1000;
    RecursionBudget b(lim, 100000);

    b.note_expanded(900);
    CHECK(b.may_descend(1, 50) == RecursionVerdict::Allow);
    b.note_expanded(200);   // now over
    CHECK(b.may_descend(1, 50) == RecursionVerdict::TotalBytesExceeded);
}

TEST(recursion_budget_stops_on_live_bytes_along_the_path)
{
    // Depth-first keeps only the current root->leaf path alive; this is the
    // limit that bounds peak memory.
    RecursionLimits lim  = wide();
    lim.max_live_bytes   = 1000;
    RecursionBudget b(lim, 100000);

    b.enter(600);
    CHECK_EQ(b.live_bytes(), static_cast<uint64_t>(600));
    CHECK(b.may_descend(1, 300) == RecursionVerdict::Allow);
    CHECK(b.may_descend(1, 500) == RecursionVerdict::LiveBytesExceeded);
}

TEST(recursion_budget_live_bytes_are_released_on_the_way_back_up)
{
    // Without this, a wide tree of small archives would exhaust the live budget
    // even though only one path is ever held at a time.
    RecursionLimits lim = wide();
    lim.max_live_bytes  = 1000;
    RecursionBudget b(lim, 100000);

    b.enter(800);
    CHECK(b.may_descend(1, 500) == RecursionVerdict::LiveBytesExceeded);
    b.leave(800);
    CHECK_EQ(b.live_bytes(), static_cast<uint64_t>(0));
    CHECK(b.may_descend(1, 500) == RecursionVerdict::Allow);
}

TEST(recursion_budget_stops_on_nested_archive_count)
{
    RecursionLimits lim      = wide();
    lim.max_nested_archives  = 2;
    RecursionBudget b(lim, 100000);

    b.enter(1); b.leave(1);
    b.enter(1); b.leave(1);
    CHECK_EQ(b.nested_seen(), 2);
    CHECK(b.may_descend(1, 1) == RecursionVerdict::CountExceeded);
}

TEST(recursion_budget_stops_on_expansion_ratio)
{
    // The zip-bomb guard: a tiny root that has already produced far more than
    // its own size must not be allowed to keep going.
    RecursionLimits lim       = wide();
    lim.max_expansion_ratio   = 10;
    RecursionBudget b(lim, 100);   // root archive is 100 bytes

    b.note_expanded(500);          // 5x — fine
    CHECK(b.may_descend(1, 10) == RecursionVerdict::Allow);
    b.note_expanded(700);          // 12x — over the 10x ceiling
    CHECK(b.may_descend(1, 10) == RecursionVerdict::RatioExceeded);
}

TEST(recursion_budget_ratio_guard_tolerates_a_zero_size_root)
{
    // Division by zero would be a crash on a degenerate input; an empty root
    // simply has no ratio to police.
    RecursionLimits lim     = wide();
    lim.max_expansion_ratio = 10;
    RecursionBudget b(lim, 0);

    b.note_expanded(1000);
    CHECK(b.may_descend(1, 10) == RecursionVerdict::Allow);
}
