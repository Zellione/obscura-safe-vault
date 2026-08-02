#pragma once

// Pure planner for in-place vault compaction (Phase 60). No I/O — operates on
// (offset, length) spans only, so the packing logic is unit- and
// property-testable without a vault.
//
// Crash-safety contract (the reason this module exists): plan_pass computes
// moves whose destinations lie in the FROZEN free list — the dead space of the
// layout described by `units`/`pinned` AT CALL TIME, which the caller
// guarantees matches the last-committed index. Space vacated by this pass's
// own moves is never allocated within the pass; the caller commits the index
// between passes, which is what makes that space legally dead for the next one.

#include <cstdint>
#include <span>
#include <vector>

namespace vault::compact_plan {

// One movable on-disk span (an image's data or thumb chunk, one video chunk,
// or a poster). `id` is the caller's stable identity (index into its table).
struct Unit {
    uint64_t offset = 0;
    uint64_t length = 0;
    uint32_t id     = 0;
};

// Move unit `unit_id` to `dest`. Destinations of a pass, applied in order,
// never touch a frozen-live or pinned byte and never overlap each other.
struct Move {
    uint32_t unit_id = 0;
    uint64_t dest    = 0;
};

// Plan one packing pass. `floor` = first data-region byte (HEADER_SIZE).
// `pinned` = live spans that must not be moved OR written over (the active
// index blob). Units are placed tail-first (descending offset) into the
// earliest frozen free range that fully fits them at a strictly lower offset.
// An empty result means the layout is converged (residual holes are smaller
// than every unit positioned after them).
[[nodiscard]] std::vector<Move> plan_pass(std::vector<Unit> units,
                                          uint64_t floor,
                                          std::span<const Unit> pinned);

// End of the last live unit byte (`floor` when units is empty): the truncation
// anchor after packing.
[[nodiscard]] uint64_t live_end(std::span<const Unit> units, uint64_t floor);

}  // namespace vault::compact_plan
