#pragma once

#include <string>

namespace vault { struct IndexNode; }

namespace ui {

// Recursive tally of a gallery subtree's contents, used by the delete-confirm
// popup to tell the user exactly what a gallery deletion will remove.
struct SubtreeCounts {
    int images = 0;
    int videos = 0;
    int galleries = 0;   // every nested gallery, at any depth
};

// Walk `node`'s descendants, adding their images/videos/sub-galleries into `c`.
// `node` itself is not counted (it is the thing being deleted).
void count_subtree(const vault::IndexNode& node, SubtreeCounts& c);

// Human summary like "12 images, 2 videos, 3 sub-galleries"; zero counts are
// dropped and the plural/singular form follows each count. An empty subtree
// renders as "nothing".
[[nodiscard]] std::string describe_subtree(const SubtreeCounts& c);

}  // namespace ui
