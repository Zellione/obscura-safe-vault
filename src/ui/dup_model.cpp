#include "ui/dup_model.h"

#include <algorithm>
#include <array>
#include <bit>
#include <numeric>

namespace ui {

namespace {

// Average luma of the box covering grid cell (cx, cy) of a gw x gh grid.
uint8_t cell_luma(std::span<const uint8_t> rgb, int w, int h,
                  int cx, int cy, int gw, int gh)
{
    const int x0 = cx * w / gw;
    const int x1 = std::max(x0 + 1, (cx + 1) * w / gw);
    const int y0 = cy * h / gh;
    const int y1 = std::max(y0 + 1, (cy + 1) * h / gh);
    uint64_t sum = 0;
    uint64_t n = 0;
    for (int y = y0; y < std::min(y1, h); ++y)
        for (int x = x0; x < std::min(x1, w); ++x) {
            const uint8_t* p = &rgb[(static_cast<size_t>(y) * w + x) * 3];
            sum += (2u * p[0] + 5u * p[1] + p[2]) / 8u;  // cheap integer luma
            ++n;
        }
    return static_cast<uint8_t>(n ? sum / n : 0);
}

} // namespace

uint64_t dhash64(std::span<const uint8_t> rgb, int w, int h)
{
    if (w < 1 || h < 1 || rgb.size() < static_cast<size_t>(w) * h * 3) return 0;
    std::array<std::array<uint8_t, 9>, 8> grid{};
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 9; ++x) grid[y][x] = cell_luma(rgb, w, h, x, y, 9, 8);
    uint64_t bits = 0;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            if (grid[y][x] < grid[y][x + 1]) bits |= uint64_t{1} << (y * 8 + x);
    return bits;
}

int hamming64(uint64_t a, uint64_t b) { return std::popcount(a ^ b); }

std::vector<std::vector<size_t>> cluster_similar(std::span<const uint64_t> hashes,
                                                 int max_bits)
{
    // Union-find over indices; O(n^2) pair scan is fine (n = images actually
    // hashed, and the scan already cost a decrypt+decode per image).
    std::vector<size_t> parent(hashes.size());
    std::iota(parent.begin(), parent.end(), size_t{0});
    auto find = [&](size_t i) {
        while (parent[i] != i) {
            parent[i] = parent[parent[i]];   // path halving
            i = parent[i];
        }
        return i;
    };
    for (size_t i = 0; i < hashes.size(); ++i)
        for (size_t j = i + 1; j < hashes.size(); ++j)
            if (hamming64(hashes[i], hashes[j]) <= max_bits)
                parent[find(i)] = find(j);

    std::vector<std::vector<size_t>> by_root(hashes.size());
    for (size_t i = 0; i < hashes.size(); ++i) by_root[find(i)].push_back(i);
    std::vector<std::vector<size_t>> out;
    for (auto& c : by_root)
        if (c.size() >= 2) {
            std::ranges::sort(c);
            out.push_back(std::move(c));
        }
    return out;
}

uint64_t group_reclaimable(const DupGroup& g)
{
    uint64_t sum = 0;
    uint64_t biggest = 0;
    for (const DupMember& m : g.members) {
        sum += m.bytes;
        biggest = std::max(biggest, m.bytes);
    }
    return sum - biggest;
}

DupReview::DupReview(std::vector<DupGroup> groups) : groups_(std::move(groups))
{
    std::ranges::stable_sort(groups_, [](const DupGroup& a, const DupGroup& b) {
        return group_reclaimable(a) > group_reclaimable(b);
    });
    // Default marks: keep the first member of each group, pre-mark the rest
    // REMOVE — a review applied untouched dedups down to one copy per group.
    for (DupGroup& g : groups_)
        for (size_t i = 0; i < g.members.size(); ++i)
            g.members[i].keep = (i == 0);
}

const std::vector<DupGroup>& DupReview::groups() const noexcept { return groups_; }

bool DupReview::toggle(size_t g, size_t m)
{
    if (g >= wave_size() || m >= groups_[g].members.size()) return false;
    DupMember& member = groups_[g].members[m];
    // Refuse to unmark the group's last keeper: at least one copy always
    // stays KEEP, so the all-REMOVE state is unreachable via toggling.
    if (member.keep) {
        const auto keepers = std::ranges::count_if(
            groups_[g].members, [](const DupMember& x) { return x.keep; });
        if (keepers <= 1) return false;
    }
    member.keep = !member.keep;
    touched_ = true;
    return true;
}

void DupReview::keep_only(size_t g, size_t m)
{
    if (g >= wave_size() || m >= groups_[g].members.size()) return;
    for (size_t i = 0; i < groups_[g].members.size(); ++i)
        groups_[g].members[i].keep = (i == m);
    touched_ = true;
}

bool DupReview::group_all_removed(size_t g) const
{
    if (g >= groups_.size()) return false;
    return std::ranges::none_of(groups_[g].members,
                                [](const DupMember& m) { return m.keep; });
}

bool DupReview::any_marked() const
{
    for (size_t g = 0; g < wave_size(); ++g)
        for (const DupMember& m : groups_[g].members)
            if (!m.keep) return true;
    return false;
}

bool DupReview::can_apply() const
{
    if (!any_marked()) return false;
    for (size_t g = 0; g < wave_size(); ++g)
        if (group_all_removed(g)) return false;
    return true;
}

size_t DupReview::marked_count() const
{
    size_t n = 0;
    for (size_t g = 0; g < wave_size(); ++g)
        for (const DupMember& m : groups_[g].members)
            if (!m.keep) ++n;
    return n;
}

uint64_t DupReview::marked_bytes() const
{
    uint64_t n = 0;
    for (size_t g = 0; g < wave_size(); ++g)
        for (const DupMember& m : groups_[g].members)
            if (!m.keep) n += m.bytes;
    return n;
}

std::vector<std::string> DupReview::marked_paths() const
{
    std::vector<std::string> out;
    for (size_t g = 0; g < wave_size(); ++g)
        for (const DupMember& m : groups_[g].members)
            if (!m.keep) out.push_back(m.node_path);
    return out;
}

size_t DupReview::wave_size() const noexcept
{
    return std::min(DUP_WAVE_GROUPS, groups_.size());
}

size_t DupReview::wave_count() const noexcept
{
    return waves_done_ + (groups_.size() + DUP_WAVE_GROUPS - 1) / DUP_WAVE_GROUPS;
}

void DupReview::finish_wave()
{
    if (groups_.empty()) return;
    groups_.erase(groups_.begin(),
                  groups_.begin() + static_cast<std::ptrdiff_t>(wave_size()));
    ++waves_done_;
    touched_ = false;
}

void DupReview::refresh_members(const std::function<bool(DupMember&)>& fn)
{
    for (DupGroup& g : groups_)
        std::erase_if(g.members, [&fn](DupMember& m) { return !fn(m); });
    std::erase_if(groups_, [](const DupGroup& g) { return g.members.size() < 2; });
    for (DupGroup& g : groups_)
        for (size_t i = 0; i < g.members.size(); ++i)
            g.members[i].keep = (i == 0);
}

bool duration_close(uint64_t a_us, uint64_t b_us) noexcept
{
    const uint64_t hi = std::max(a_us, b_us);
    const uint64_t lo = std::min(a_us, b_us);
    const uint64_t tol = std::max<uint64_t>(
        static_cast<uint64_t>(static_cast<double>(hi) * DUP_VID_DURATION_TOL),
        DUP_VID_DURATION_ABS_US);
    return hi - lo <= tol;
}

bool video_sig_match(const VideoSig& a, const VideoSig& b) noexcept
{
    const uint32_t common = a.frame_valid & b.frame_valid;
    int shared = 0;
    int matched = 0;
    for (size_t i = 0; i < DUP_VID_FRAME_POSITIONS.size(); ++i) {
        if (!(common & (1u << i))) continue;
        ++shared;
        if (hamming64(a.frame_hash[i], b.frame_hash[i]) <= DUP_VID_FRAME_MAX_BITS) ++matched;
    }
    if (shared >= DUP_VID_MIN_MATCHED) return matched >= DUP_VID_MIN_MATCHED;
    return a.poster_ok && b.poster_ok &&
           hamming64(a.poster_hash, b.poster_hash) <= DUP_VID_POSTER_MAX_BITS;
}

std::vector<std::vector<size_t>> cluster_video_sigs(std::span<const VideoSig> sigs,
                                                    std::span<const uint64_t> duration_us)
{
    // Union-find over indices, same shape as cluster_similar; the pair scan is
    // O(n^2) over videos that already passed the cheap prefilters.
    std::vector<size_t> parent(sigs.size());
    std::iota(parent.begin(), parent.end(), size_t{0});
    auto find = [&](size_t i) {
        while (parent[i] != i) {
            parent[i] = parent[parent[i]];   // path halving
            i = parent[i];
        }
        return i;
    };
    for (size_t i = 0; i < sigs.size(); ++i)
        for (size_t j = i + 1; j < sigs.size(); ++j)
            if (duration_close(duration_us[i], duration_us[j]) &&
                video_sig_match(sigs[i], sigs[j]))
                parent[find(i)] = find(j);

    std::vector<std::vector<size_t>> by_root(sigs.size());
    for (size_t i = 0; i < sigs.size(); ++i) by_root[find(i)].push_back(i);
    std::vector<std::vector<size_t>> out;
    for (auto& c : by_root)
        if (c.size() >= 2) {
            std::ranges::sort(c);
            out.push_back(std::move(c));
        }
    return out;
}

} // namespace ui
