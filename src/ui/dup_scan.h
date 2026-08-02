#pragma once

// Duplicate-scan snapshot + background hashing worker.
//
// Threading contract: collect_scan_items() is MAIN-THREAD ONLY (index tree).
// DupScanJob's worker touches the vault exclusively through
// vault::read_thumb_span — the thread-safe, any-thread chunk-span decryptor
// (thumb_fp_ + thumb_mutex_, Phase 58) — over the snapshot's copied spans, so
// it never holds an IndexNode* and never races tree mutations.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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

struct DupScanOutcome {
    std::vector<DupGroup> groups;
    size_t skipped   = 0;
    bool   cancelled = false;
};

// Background hashing worker. start() spawns one jthread; the worker reads
// chunk spans via vault::read_thumb_span only (any-thread safe). Progress is
// atomically published; the outcome is taken once after active() turns false.
class DupScanJob {
public:
    DupScanJob() = default;
    ~DupScanJob();
    DupScanJob(const DupScanJob&)            = delete;
    DupScanJob& operator=(const DupScanJob&) = delete;

    void start(const vault::Vault& v, std::vector<DupScanItem> items, bool perceptual);
    [[nodiscard]] bool active() const { return running_.load(); }
    void cancel() { cancel_.store(true); }

    [[nodiscard]] size_t progress_done() const  { return done_.load(); }
    [[nodiscard]] size_t progress_total() const { return total_.load(); }
    [[nodiscard]] std::string current_name() const;
    [[nodiscard]] std::optional<DupScanOutcome> take_outcome();

private:
    void run(const vault::Vault& v, std::vector<DupScanItem> items, bool perceptual);

    std::jthread              thread_;
    std::atomic<bool>         running_{false};
    std::atomic<bool>         cancel_{false};
    std::atomic<size_t>       done_{0}, total_{0};
    mutable std::mutex        mtx_;      // guards current_ + outcome_
    std::string               current_;
    std::optional<DupScanOutcome> outcome_;
};

} // namespace ui
