#include "compact_plan.h"

#include <algorithm>

namespace vault::compact_plan {

namespace {

struct FreeRange {
    uint64_t start = 0;
    uint64_t length = 0;
};

// Free list = complement of all live spans within [floor, last live end).
// Live spans never overlap (they come from a valid committed index), so a
// sort + linear sweep suffices.
std::vector<FreeRange> free_ranges(std::span<const Unit> units, std::span<const Unit> pinned,
                                   uint64_t floor)
{
    std::vector<std::pair<uint64_t, uint64_t>> live;  // (offset, length)
    live.reserve(units.size() + pinned.size());
    for (const Unit& u : units)
        live.emplace_back(u.offset, u.length);
    for (const Unit& p : pinned)
        live.emplace_back(p.offset, p.length);
    std::ranges::sort(live);

    std::vector<FreeRange> out;
    uint64_t cursor = floor;
    for (const auto& [off, len] : live) {
        if (off > cursor) out.push_back({cursor, off - cursor});
        cursor = std::max(cursor, off + len);
    }
    return out;
}

// Find a suitable free range for a unit via earliest-fit search.
// Ranges are ascending by start position; stop early if start >= u.offset.
FreeRange* find_suitable_free_range(const Unit& u, std::vector<FreeRange>& free)
{
    for (FreeRange& r : free) {
        if (r.start >= u.offset) return nullptr;  // ranges are ascending; no dest below src left
        if (r.length < u.length) continue;
        return &r;
    }
    return nullptr;
}

}  // namespace

std::vector<Move> plan_pass(std::vector<Unit> units, uint64_t floor, std::span<const Unit> pinned)
{
    std::vector<FreeRange> free = free_ranges(units, pinned, floor);
    std::vector<Move> moves;
    if (free.empty()) return moves;

    // Tail-first: shrinking the file end early means a cancelled run has
    // already reclaimed the most (truncation cuts from the tail).
    std::ranges::sort(units, {}, &Unit::offset);
    // Fast reject: nothing can fit once every remaining range is too small.
    uint64_t max_free = 0;
    for (const FreeRange& r : free)
        max_free = std::max(max_free, r.length);

    for (auto it = units.rbegin(); it != units.rend(); ++it) {
        const Unit& u = *it;
        if (u.length > max_free) continue;
        if (FreeRange* r = find_suitable_free_range(u, free); r != nullptr) {
            moves.push_back({u.id, r->start});
            r->start += u.length;  // frozen list only ever shrinks
            r->length -= u.length;
        }
        if (moves.size() == units.size()) break;
        max_free = 0;
        for (const FreeRange& fr : free)
            max_free = std::max(max_free, fr.length);
    }
    return moves;
}

uint64_t live_end(std::span<const Unit> units, uint64_t floor)
{
    uint64_t end = floor;
    for (const Unit& u : units)
        end = std::max(end, u.offset + u.length);
    return end;
}

}  // namespace vault::compact_plan
