#pragma once

#include "ui/import_model.h"
#include "vault/commit_lane.h"
#include "vault/op_progress.h"
#include "vault/vault.h"
#include "crypto/secure_mem.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace ui {

// App-owned background import pipeline (Phase 50). One worker thread runs
// tasks FIFO; per image file, decode+thumbnail runs on a small pool while
// staging (encrypt+append) and record posting stay strictly in file order.
// The main thread pumps drain() every frame to apply staged records to the
// index tree (tree stays main-thread-only) and commits in batches via the
// embedded CommitLane.
class ImportQueue {
public:
    ImportQueue();
    ~ImportQueue();                       // end_session() if needed
    ImportQueue(const ImportQueue&)            = delete;
    ImportQueue& operator=(const ImportQueue&) = delete;

    // ---- session (main thread) ----
    // Bind to the freshly unlocked vault; starts worker + commit lane and
    // installs the commit router while busy. One session per unlocked vault.
    void begin_session(vault::Vault& v);
    // Cancel everything, finish the in-flight file, flush the lane, uninstall
    // the router, join threads, wipe queued passwords. Blocking; called before
    // lock/switch/quit (after user confirm) and from end_session().
    void abort_and_flush();
    void end_session();                   // abort_and_flush + drop binding

    // ---- enqueue (main thread; prompts already resolved) ----
    uint64_t enqueue_files(std::vector<std::filesystem::path> files,
                           std::string dest_gallery);
    uint64_t enqueue_archive(std::filesystem::path archive, std::string dest_gallery,
                             std::string gallery_name, ImportTaskKind kind,
                             bool password_protected = false,
                             crypto::SecureBytes password = {});

    // ---- queue control (main thread) ----
    [[nodiscard]] bool cancel(uint64_t id);          // queued: drop; running: coop-cancel
    [[nodiscard]] bool reorder(uint64_t id, int delta);
    void clear_finished();
    // An exclusive vault op (FileOpJob delete/transfer/compact…) is active;
    // the worker must not START a new task while set. Synchronous: takes the
    // queue mutex, so after set_exclusive(true) returns no new task starts.
    void set_exclusive(bool held);

    // ---- per-frame pump (main thread) ----
    // Apply staged records (ensure_gallery_path/attach_staged), advance the
    // batch policy, enqueue lane snapshots when due. Returns the number of
    // records applied (callers refresh screens when > 0).
    int drain(double dt);

    // ---- observation (main thread / render) ----
    [[nodiscard]] bool busy() const;                 // running or queued work
    [[nodiscard]] bool lane_failed() const;
    [[nodiscard]] std::vector<ImportTaskInfo> snapshot() const;
    [[nodiscard]] std::string footer_summary() const;   // footer_import_summary(...)

private:
    struct Task;
    struct StagedRecord;
    class StagingSink;
    class DecodePool;

    // Worker thread function
    void worker_loop();

    // Task processing
    void process_files_task(Task& task);
    void process_archive_task(Task& task);

    // Internal state helpers
    void maybe_end_batch();

    // Synchronization
    mutable std::mutex mu_;
    std::condition_variable worker_cv_;
    std::condition_variable main_cv_;

    // Core state
    vault::Vault* v_ = nullptr;
    std::unique_ptr<vault::CommitLane> lane_;
    std::jthread worker_;

    // Task management
    std::deque<Task> tasks_;
    uint64_t next_task_id_ = 1;
    size_t worker_task_index_ = 0;  // Index in tasks_ of the task being/to-be processed
    bool exclusive_ = false;
    bool worker_stop_ = false;

    // Record queue for main thread to drain
    std::deque<StagedRecord> records_;

    // Batch commit policy
    BatchCommitPolicy policy_;

    // Decode pool for images
    std::unique_ptr<DecodePool> decode_pool_;
};

}  // namespace ui
