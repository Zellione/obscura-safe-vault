#pragma once

// Pure, SDL-free duplicate-detection model (Phase: duplicate finder).
// Hash primitives + grouping + review-marking state. No I/O, no vault types —
// fully unit-testable headless.

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ui {

// Perceptual-match threshold: max Hamming distance (bits of 64) between two
// dHashes still considered "the same picture".
inline constexpr int DUP_SIMILAR_MAX_BITS = 5;

// Wave window (Phase 64): the review is resolved in waves of at most this
// many groups; the current wave is always the FIRST wave_size() entries of
// groups(). Marks beyond it are unreachable until finish_wave().
inline constexpr size_t DUP_WAVE_GROUPS = 20;

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
    uint32_t    width  = 0;
    uint32_t    height = 0;
    uint64_t    thumb_offset = 0;   // tile span; 0 length = none
    uint64_t    thumb_length = 0;
    std::vector<std::pair<uint64_t, uint64_t>> data_spans;  // (offset,length) for full original
    bool        keep = true;
};
struct DupGroup {
    enum class Kind : uint8_t { Identical, Similar, SimilarVideo };
    Kind kind = Kind::Identical;
    int  distance_bits = 0;    // max pairwise Hamming (Similar only)
    std::vector<DupMember> members;
};
[[nodiscard]] uint64_t group_reclaimable(const DupGroup& g); // sum(bytes) - max(bytes)

class DupReview {
public:
    DupReview() = default;
    // Sorts groups largest-reclaimable first, then applies the default marks:
    // the first member of each group is KEEP, every other member REMOVE.
    explicit DupReview(std::vector<DupGroup> groups);
    [[nodiscard]] const std::vector<DupGroup>& groups() const noexcept;
    // Flip one member's KEEP/REMOVE mark. Refused (returns false, no change)
    // when the member is its group's last keeper or g/m is out of range.
    bool toggle(size_t g, size_t m);
    void keep_only(size_t g, size_t m);      // member m KEEP, siblings REMOVE
    // True once the USER changed any mark (toggle/keep_only) — the ctor's
    // pre-applied defaults don't count. Gates the leave-confirm and the
    // idle-lock suppression in the review screen.
    [[nodiscard]] bool touched() const noexcept { return touched_; }
    [[nodiscard]] bool group_all_removed(size_t g) const;
    [[nodiscard]] bool any_marked() const;
    [[nodiscard]] bool can_apply() const;    // any_marked() && no fully-removed group
    [[nodiscard]] size_t   marked_count() const;
    [[nodiscard]] uint64_t marked_bytes() const;
    [[nodiscard]] std::vector<std::string> marked_paths() const;

    // ---- Wave window (Phase 64). Scoped queries: any_marked / can_apply /
    // marked_* / toggle / keep_only all operate on the current wave only.
    [[nodiscard]] size_t wave_size()  const noexcept;
    [[nodiscard]] size_t wave_index() const noexcept { return waves_done_; }
    [[nodiscard]] size_t wave_count() const noexcept;
    // Complete the current wave (applied or skipped): erase its groups,
    // count it finished, reset touched(). No-op when groups() is empty.
    void finish_wave();

private:
    std::vector<DupGroup> groups_;
    bool                  touched_ = false;
    size_t                waves_done_ = 0;
};

// ---- Phase 62: perceptual video duplicates ----
//
// A video pair may only match when duration-close; frame evidence (sampled
// dHashes at fixed timeline fractions) decides when both sides carry it, and
// the stored-poster dHash is the fallback verdict otherwise (non-FFmpeg
// builds never fill frames).

inline constexpr float    DUP_VID_DURATION_TOL     = 0.02f;      // relative
inline constexpr uint64_t DUP_VID_DURATION_ABS_US  = 500'000;    // short-clip floor
inline constexpr int      DUP_VID_POSTER_MAX_BITS  = 10;
inline constexpr std::array<float, 5> DUP_VID_FRAME_POSITIONS{0.10f, 0.30f, 0.50f, 0.70f, 0.90f};
inline constexpr int      DUP_VID_FRAME_MAX_BITS   = 7;
inline constexpr int      DUP_VID_MIN_MATCHED      = 4;

struct VideoSig {
    uint64_t                poster_hash = 0;
    bool                    poster_ok   = false;
    std::array<uint64_t, 5> frame_hash{};
    uint32_t                frame_valid = 0;   // bitmask, bit i = position i decoded
};

[[nodiscard]] bool duration_close(uint64_t a_us, uint64_t b_us) noexcept;

// Match verdict for a duration-close pair. Frame evidence wins when both
// sides share >= DUP_VID_MIN_MATCHED valid positions; otherwise falls back
// to the poster verdict (poster_ok on both && hamming <= POSTER_MAX_BITS).
[[nodiscard]] bool video_sig_match(const VideoSig& a, const VideoSig& b) noexcept;

// Cluster videos whose signatures match pairwise (duration_close &&
// video_sig_match) via union-find; `duration_us[i]` parallels `sigs[i]`.
// Returns clusters of positions into `sigs` with >= 2 members, ordered by
// first index.
[[nodiscard]] std::vector<std::vector<size_t>>
cluster_video_sigs(std::span<const VideoSig> sigs, std::span<const uint64_t> duration_us);

} // namespace ui
