#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

// Split "a/b/c" into {"a","b","c"} (empty segments dropped). join_path is inverse.
[[nodiscard]] std::vector<std::string> split_path(std::string_view path);
[[nodiscard]] std::string              join_path(std::span<const std::string> segs);

// Current location in the gallery tree (a stack of names) plus a clamped
// selection index over the current gallery's children. Pure; no SDL.
class NavModel {
public:
    void enter(std::string segment);   // descend; selection resets to 0
    bool up() noexcept;                // ascend; false if already at root

    [[nodiscard]] std::string                  path() const;       // "" at root
    [[nodiscard]] std::span<const std::string> segments() const noexcept { return segments_; }

    void set_count(int n) noexcept;    // child count; re-clamps selection
    void move(int delta) noexcept;     // selection += delta, clamped
    void select(int index) noexcept;   // selection = index, clamped
    [[nodiscard]] int selected() const noexcept { return selected_; }
    [[nodiscard]] int count() const noexcept { return count_; }

private:
    void clamp() noexcept;

    std::vector<std::string> segments_;
    int                      count_    = 0;
    int                      selected_ = 0;
};

} // namespace ui
