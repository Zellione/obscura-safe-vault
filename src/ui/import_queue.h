#pragma once

#include "crypto/secure_mem.h"
#include "ui/import_model.h"
#include "vault/commit_lane.h"
#include "vault/index.h"
#include "vault/op_progress.h"
#include "vault/vault.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
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

    // Test seam: joins worker WITHOUT final lane flush, simulating a crash
    // between batch commits. Never called by production code.
    friend void test_only_drop_without_flush(ImportQueue& q);

private:
    // Nested types: defined in-header for MSVC's std::deque to require complete element types at member declaration
    struct StagedRecord {
        std::string gallery_path;
        std::optional<vault::IndexNode> node;  // nullopt => ensure gallery only
        uint64_t task_id = 0;
        bool counted = false;
    };

    struct Task {
        uint64_t id = 0;
        ImportTaskKind kind = ImportTaskKind::Files;
        std::string display_name;
        std::string dest_gallery;
        std::vector<std::filesystem::path> files;  // for Files task
        std::filesystem::path archive_path;
        std::string gallery_name;
        crypto::SecureBytes password;

        ImportTaskState state = ImportTaskState::Queued;
        int imported = 0, skipped = 0;
        std::string error;

        // Progress tracking (shared_ptr to work around OpProgress's atomic members)
        std::shared_ptr<vault::OpProgress> progress;

        [[nodiscard]] bool finished() const noexcept
        {
            return state == ImportTaskState::Done || state == ImportTaskState::Failed ||
                   state == ImportTaskState::Cancelled;
        }
    };

    class StagingSink;
    class DecodePool;

    // Worker thread function
    void worker_loop();

    // Task processing
    void process_files_task(Task& task);
    void process_archive_task(Task& task);

    // Internal state helpers
    void maybe_end_batch();
    void mark_task_skipped(uint64_t task_id);
    void mark_task_imported(uint64_t task_id);
    bool has_available_work() const;  // Called under lock
    bool extract_task_data(uint64_t& out_task_id, ImportTaskKind& out_task_kind,
                          std::vector<std::filesystem::path>& out_files,
                          std::filesystem::path& out_archive_path,
                          std::string& out_gallery_name, std::string& out_dest_gallery,
                          std::shared_ptr<vault::OpProgress>& out_progress);  // Called under lock
    void mark_task_complete(uint64_t task_id, const std::shared_ptr<vault::OpProgress>& progress);  // Called under lock

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
    std::atomic<bool> exclusive_{false};
    std::atomic<bool> worker_stop_{false};
    bool aborted_ = false;  // Tracks abort_and_flush idempotence (checked under mu_)

    // Record queue for main thread to drain
    std::deque<StagedRecord> records_;

    // Batch commit policy
    BatchCommitPolicy policy_;

    // Decode pool for images
    std::unique_ptr<DecodePool> decode_pool_;
};

}  // namespace ui
