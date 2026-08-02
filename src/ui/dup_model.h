#pragma once

// Pure, SDL-free duplicate-detection model (Phase: duplicate finder).
// Hash primitives + grouping + review-marking state. No I/O, no vault types —
// fully unit-testable headless.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ui {

// Perceptual-match threshold: max Hamming distance (bits of 64) between two
// dHashes still considered "the same picture".
inline constexpr int DUP_SIMILAR_MAX_BITS = 5;

// 64-bit difference hash over 3-channel RGB row-major pixels: box-sample to a
// 9x8 grayscale grid, then bit (y*8+x) = grid[y][x] < grid[y][x+1].
// Scale-invariant by construction; any w,h >= 1 is safe.
[[nodiscard]] uint64_t dhash64(std::span<const uint8_t> rgb, int w, int h);

[[nodiscard]] int hamming64(uint64_t a, uint64_t b);

// Union-find transitive clustering: i,j joined when their hashes are within
// max_bits. Only clusters of >= 2 indices are returned, each sorted ascending.
[[nodiscard]] std::vector<std::vector<size_t>>
cluster_similar(std::span<const uint64_t> hashes, int max_bits);

} // namespace ui
