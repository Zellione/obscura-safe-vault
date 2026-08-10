#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vault { class Vault; }
namespace gfx { class Renderer; class FontAtlas; }

namespace ui {

// Pure helpers for the Phase 74 multi-select Del flow — shared by the gallery
// grid and CollectionBatchOps so the confirm-modal numbers cannot drift.

// Drop every path that lies INSIDE another selected path (a collection-screen
// selection can hold gallery "g" and "g/a.png"; submitting both would make the
// batch report a phantom missing item). Component-boundary safe: "g2" is not a
// descendant of "g". Input order is preserved.
[[nodiscard]] std::vector<std::string> prune_descendant_paths(std::vector<std::string> paths);

// Aggregate of what a batch delete will remove, for the confirm modal and the
// progress bar. Galleries count recursively (a selected gallery contributes
// itself + every nested one); bytes are summed plaintext orig_size.
struct BatchDeleteSummary {
    int      top_level  = 0;  // resolving top-level paths (the modal title count)
    int      galleries  = 0;
    int      images     = 0;
    int      videos     = 0;
    uint64_t bytes      = 0;
    int      item_total = 0;  // progress-bar units (media + galleries, all depths)
};

// Resolve `paths` against the live index and tally. Non-resolving paths are
// skipped (they are already gone — deleting them is a no-op).
[[nodiscard]] BatchDeleteSummary summarize_batch_delete(const vault::Vault& v,
                                                        std::span<const std::string> paths);

// "2 galleries · 7 images · 3 videos · 312 MB" — zero categories dropped,
// singular/plural per count; bytes always shown via format_size.
[[nodiscard]] std::string batch_delete_counts_line(const BatchDeleteSummary& s);

// Default-cancel DANGER confirm modal for a batch delete: title with the
// top-level count, the counts line, the irreversibility warning, [Esc/N]/[Y]
// keybar. Drawing-only plumbing (ConsentDialog precedent) — key handling is
// the caller's.
void draw_batch_delete_confirm(gfx::Renderer& r, gfx::FontAtlas& font,
                               float W, float H, const BatchDeleteSummary& s);

}  // namespace ui
