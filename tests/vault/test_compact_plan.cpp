#include "test_framework.h"

#include <algorithm>
#include <random>
#include <vector>

#include "vault/compact_plan.h"

using vault::compact_plan::Unit;
using vault::compact_plan::Move;
using vault::compact_plan::plan_pass;
using vault::compact_plan::live_end;

namespace {
constexpr uint64_t FLOOR = 1024;  // stand-in for HEADER_SIZE

// Apply moves to a copy of the units (id == index into `units`).
void apply(std::vector<Unit>& units, const std::vector<Move>& moves)
{
    for (const Move& m : moves) units[m.unit_id].offset = m.dest;
}

// True iff [a, a+al) and [b, b+bl) intersect.
bool overlap(uint64_t a, uint64_t al, uint64_t b, uint64_t bl)
{
    return a < b + bl && b < a + al;
}
}  // namespace

TEST(plan_slides_unit_down_into_leading_gap)
{
    // [gap 300][A:200] -> the gap fully fits A, so A slides to FLOOR.
    std::vector<Unit> units{{FLOOR + 300, 200, 0}};
    auto moves = plan_pass(units, FLOOR, {});
    REQUIRE(moves.size() == 1);
    CHECK_EQ(moves[0].unit_id, 0u);
    CHECK_EQ(moves[0].dest, FLOOR);
    apply(units, moves);
    CHECK_EQ(live_end(units, FLOOR), FLOOR + 200);
}

TEST(plan_leaves_unit_when_no_gap_fits)
{
    // Gap of 100 before a 200-byte unit: nothing fits, no move (the
    // overlapping slide is exactly what the crash-safety rule forbids).
    std::vector<Unit> units{{FLOOR + 100, 200, 0}};
    auto moves = plan_pass(units, FLOOR, {});
    CHECK_TRUE(moves.empty());
}

TEST(plan_fills_gap_from_the_tail)
{
    // [gap 100][A:200][B:100 at tail] -> B (tail-first) fills the gap; A can't.
    std::vector<Unit> units{{FLOOR + 100, 200, 0}, {FLOOR + 300, 100, 1}};
    auto moves = plan_pass(units, FLOOR, {});
    REQUIRE(moves.size() == 1);
    CHECK_EQ(moves[0].unit_id, 1u);
    CHECK_EQ(moves[0].dest, FLOOR);
    apply(units, moves);
    CHECK_EQ(live_end(units, FLOOR), FLOOR + 300);  // tail shrank by 100
}

TEST(plan_never_targets_pinned_spans)
{
    // Gap is occupied by a pinned span (the active index blob): no move.
    std::vector<Unit> units{{FLOOR + 100, 50, 0}};
    std::vector<Unit> pinned{{FLOOR, 100, 0}};
    auto moves = plan_pass(units, FLOOR, pinned);
    CHECK_TRUE(moves.empty());
}

TEST(plan_second_pass_reclaims_space_vacated_by_first)
{
    // [gap 100][A:100][gap 100][B:200]. Pass 1 (frozen free = two 100 gaps,
    // tail-first): B fits neither; A fits the first -> A moves to FLOOR.
    // A's vacated span merges with the second gap, so pass 2 sees one 200
    // free range and B slides down behind A. This is exactly why the
    // commit-between-passes exists.
    std::vector<Unit> units{{FLOOR + 100, 100, 0}, {FLOOR + 300, 200, 1}};
    auto p1 = plan_pass(units, FLOOR, {});
    REQUIRE(p1.size() == 1);
    CHECK_EQ(p1[0].unit_id, 0u);
    CHECK_EQ(p1[0].dest, FLOOR);
    apply(units, p1);
    auto p2 = plan_pass(units, FLOOR, {});
    REQUIRE(p2.size() == 1);
    CHECK_EQ(p2[0].unit_id, 1u);
    CHECK_EQ(p2[0].dest, FLOOR + 100);
    apply(units, p2);
    CHECK_EQ(live_end(units, FLOOR), FLOOR + 300);  // fully packed
    CHECK_TRUE(plan_pass(units, FLOOR, {}).empty());  // converged
}

// The property test IS the correctness argument for crash safety — do not
// weaken it. Random layouts; for every pass, verify against the frozen
// layout that (a) every dest range is disjoint from every frozen span and
// every pinned span, (b) dests don't overlap each other, (c) after apply,
// no two live spans overlap, (d) passes strictly reduce the offset sum and
// reach a fixpoint.
TEST(plan_pass_property_random_layouts)
{
    std::mt19937 rng(0xC0FFEE);
    for (int iter = 0; iter < 200; ++iter) {
        // Build a random non-overlapping layout with random gaps.
        std::vector<Unit> units;
        uint64_t cur = FLOOR;
        const int n = 1 + static_cast<int>(rng() % 30);
        for (int i = 0; i < n; ++i) {
            cur += rng() % 5000;                       // gap (possibly 0)
            const uint64_t len = 1 + rng() % 4000;     // unit
            units.push_back({cur, len, static_cast<uint32_t>(units.size())});
            cur += len;
        }
        // Pin one pseudo index blob in a random gap-free spot past the end.
        std::vector<Unit> pinned{{cur + rng() % 1000, 300 + rng() % 500, 0}};

        for (int pass = 0; pass < 100; ++pass) {
            const std::vector<Unit> frozen = units;  // last-committed layout
            auto moves = plan_pass(units, FLOOR, pinned);
            if (moves.empty()) break;
            uint64_t sum_before = 0, sum_after = 0;
            for (const auto& u : units) sum_before += u.offset;
            for (size_t i = 0; i < moves.size(); ++i) {
                const Move& m = moves[i];
                const uint64_t len = units[m.unit_id].length;
                REQUIRE(m.dest >= FLOOR);
                REQUIRE(m.dest < frozen[m.unit_id].offset);   // strictly down
                for (const auto& f : frozen)                  // (a) frozen-live
                    REQUIRE(!overlap(m.dest, len, f.offset, f.length));
                for (const auto& p : pinned)                  // (a) pinned
                    REQUIRE(!overlap(m.dest, len, p.offset, p.length));
                for (size_t j = 0; j < i; ++j)                // (b) dest/dest
                    REQUIRE(!overlap(m.dest, len,
                                     moves[j].dest, units[moves[j].unit_id].length));
            }
            apply(units, moves);
            for (size_t a = 0; a < units.size(); ++a)          // (c) no overlap
                for (size_t b = a + 1; b < units.size(); ++b)
                    REQUIRE(!overlap(units[a].offset, units[a].length,
                                     units[b].offset, units[b].length));
            for (const auto& u : units) sum_after += u.offset;
            REQUIRE(sum_after < sum_before);                   // (d) progress
            REQUIRE(pass < 99);                                // (d) terminates
        }
    }
}
