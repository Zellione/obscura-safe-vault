#pragma once

// Duplicate-scan snapshot + background hashing worker.
//
// Threading contract: collect_scan_items() is MAIN-THREAD ONLY (index tree).
// DupScanJob's worker touches the vault exclusively through
// vault::read_thumb_span — the thread-safe, any-thread chunk-span decryptor
// (thumb_fp_ + thumb_mutex_, Phase 58) — over the snapshot's copied spans, so
// it never holds an IndexNode* and never races tree mutations.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ui/dup_model.h"

namespace vault { class Vault; }

namespace ui {

struct DupScanItem {
    std::string node_path, name, parent_path;
    bool        is_video = false;
    uint64_t    bytes = 0;
    uint32_t    width = 0, height = 0;
    std::vector<std::pair<uint64_t, uint64_t>> data_spans;  // (offset,length)
    uint64_t    thumb_offset = 0, thumb_length = 0;         // thumb / poster span
};

// Main-thread ONLY (walks the index tree via Vault::list). Every image and
// video in the whole vault, recursively.
[[nodiscard]] std::vector<DupScanItem> collect_scan_items(const vault::Vault& v);

} // namespace ui
