#include "ui/dup_model.h"

#include <algorithm>
#include <bit>
#include <numeric>

namespace ui {

namespace {

// Average luma of the box covering grid cell (cx, cy) of a gw x gh grid.
uint8_t cell_luma(std::span<const uint8_t> rgb, int w, int h,
                  int cx, int cy, int gw, int gh)
{
    const int x0 = cx * w / gw, x1 = std::max(x0 + 1, (cx + 1) * w / gw);
    const int y0 = cy * h / gh, y1 = std::max(y0 + 1, (cy + 1) * h / gh);
    uint64_t sum = 0, n = 0;
    for (int y = y0; y < std::min(y1, h); ++y)
        for (int x = x0; x < std::min(x1, w); ++x) {
            const uint8_t* p = &rgb[(static_cast<size_t>(y) * w + x) * 3];
            sum += (2u * p[0] + 5u * p[1] + p[2]) / 8u;  // cheap integer luma
            ++n;
        }
    return n ? static_cast<uint8_t>(sum / n) : 0;
}

} // namespace

uint64_t dhash64(std::span<const uint8_t> rgb, int w, int h)
{
    if (w < 1 || h < 1 || rgb.size() < static_cast<size_t>(w) * h * 3) return 0;
    uint8_t grid[8][9];
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
        while (parent[i] != i) i = parent[i] = parent[parent[i]];
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

} // namespace ui
