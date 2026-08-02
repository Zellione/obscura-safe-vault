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

// Grouping types + DupReview marking state (Phase: duplicate finder).
struct DupMember {
    std::string node_path;     // full slash-path in the vault
    std::string name;
    std::string parent_path;   // "" = root
    bool        is_video = false;
    uint64_t    bytes = 0;     // plaintext orig_size
    uint32_t    width = 0, height = 0;
    uint64_t    thumb_offset = 0, thumb_length = 0;  // tile span; 0 len = none
    bool        keep = true;
};
struct DupGroup {
    enum class Kind : uint8_t { Identical, Similar };
    Kind kind = Kind::Identical;
    int  distance_bits = 0;    // max pairwise Hamming (Similar only)
    std::vector<DupMember> members;
};
[[nodiscard]] uint64_t group_reclaimable(const DupGroup& g); // sum(bytes) - max(bytes)

class DupReview {
public:
    DupReview() = default;
    explicit DupReview(std::vector<DupGroup> groups);  // sorts reclaimable desc
    [[nodiscard]] const std::vector<DupGroup>& groups() const noexcept;
    void toggle(size_t g, size_t m);
    void keep_only(size_t g, size_t m);      // member m KEEP, siblings REMOVE
    [[nodiscard]] bool group_all_removed(size_t g) const;
    [[nodiscard]] bool any_marked() const;
    [[nodiscard]] bool can_apply() const;    // any_marked() && no fully-removed group
    [[nodiscard]] size_t   marked_count() const;
    [[nodiscard]] uint64_t marked_bytes() const;
    [[nodiscard]] std::vector<std::string> marked_paths() const;
private:
    std::vector<DupGroup> groups_;
};

} // namespace ui
