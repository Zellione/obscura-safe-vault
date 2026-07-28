#include "ui/import_queue.h"

#include "ui/recursive_exec.h"
#include "ui/spanned_zip.h"
#include "ui/volume_import.h"

#include <array>
#include "ui/archive_import.h"
#include "ui/folder_scan.h"
#include "ui/media_sink.h"
#include "ui/zip_import.h"
#include "ui/zip_plan.h"
#include "image/decode.h"
#include "image/anim_info.h"
#include "image/thumbnail.h"
#include "vault/staging.h"
#include "platform/safe_print.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <fstream>
#include <queue>
#include <thread>

namespace ui {

namespace {
    // Lookahead window constraints for resequencing
    constexpr int LOOKAHEAD_MAX_ITEMS = 8;
    constexpr size_t LOOKAHEAD_MAX_BYTES = 256 * 1024 * 1024;  // 256 MiB

    // Image decode pool: min(hardware_concurrency, 4) threads
    size_t decode_pool_size()
    {
        const size_t hw = std::thread::hardware_concurrency();
        return std::min(hw == 0 ? 2 : hw, size_t{4});
    }

    // Helper: try to read file into SecureBytes. Returns true on success, false on any error.
    bool try_read_file(const std::filesystem::path& file_path, crypto::SecureBytes& out)
    {
        std::ifstream f(file_path, std::ios::binary | std::ios::ate);
        if (!f.is_open()) return false;

        const auto sz = f.tellg();
        if (sz <= 0 || sz > 512 * 1024 * 1024) return false;  // 512 MiB max per file

        out = crypto::SecureBytes(static_cast<size_t>(sz));
        f.seekg(0);
        f.read(reinterpret_cast<char*>(out.data()), sz);
        return static_cast<bool>(f);
    }
}

// ============================================================================
// Internal structures
// ============================================================================

// Async image decode pool
class ImportQueue::DecodePool {
public:
    explicit DecodePool(size_t num_threads)
    {
        for (size_t i = 0; i < num_threads; ++i) {
            threads_.emplace_back([this]() { worker_loop(); });
        }
    }

    ~DecodePool()
    {
        {
            std::lock_guard lock(mu_);
            stopping_ = true;
        }
        cv_.notify_all();
        completion_cv_.notify_all();
        // jthreads join automatically
    }

    DecodePool(const DecodePool&) = delete;
    DecodePool& operator=(const DecodePool&) = delete;

    struct DecodeJob {
        std::vector<uint8_t> data;
        vault::StagedThumb result;
        std::atomic<bool> done{false};
    };

    void submit(const std::shared_ptr<DecodeJob>& job)
    {
        {
            std::lock_guard lock(mu_);
            queue_.push(job);
        }
        cv_.notify_all();  // Notify all workers to spread work
    }

    [[nodiscard]] bool is_done(const std::shared_ptr<DecodeJob>& job) const
    {
        return job->done.load();
    }

    // Wait for a specific job to complete (CV-based, not spin-polling)
    void wait_for_completion(const std::shared_ptr<DecodeJob>& job, int timeout_ms = 5000)
    {
        std::unique_lock lock(mu_);
        if (job->done.load())
            return;

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        completion_cv_.wait_until(lock, deadline, [&job]() { return job->done.load(); });
    }

private:
    void worker_loop()
    {
        while (true) {
            // Acquire job under lock, then release before decoding
            std::shared_ptr<DecodeJob> job;
            {
                std::unique_lock lock(mu_);
                cv_.wait(lock, [this]() { return !queue_.empty() || stopping_; });

                if (stopping_ && queue_.empty())
                    break;

                if (queue_.empty())
                    continue;

                job = queue_.front();
                queue_.pop();
            }  // Lock released here

            // Decode outside the lock
            if (auto decoded = image::decode_from_memory(job->data)) {
                job->result.width = decoded->width;
                job->result.height = decoded->height;
                job->result.format = static_cast<vault::ImageFormat>(decoded->format);

                // Make thumbnail
                if (auto thumb_bytes = image::make_thumbnail(*decoded, 256, 85)) {
                    job->result.thumb_jpeg = std::move(*thumb_bytes);
                }

                // Set the animated flag for formats that can carry animation
                job->result.animated = image::is_animated(decoded->format, job->data);
            }
            job->data.clear();  // Free the source pixels
            job->done.store(true);

            // Notify all waiters that a job is done
            {
                std::lock_guard notify_lock(mu_);
                completion_cv_.notify_all();
            }
        }
    }

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::condition_variable completion_cv_;
    std::queue<std::shared_ptr<DecodeJob>> queue_;
    std::vector<std::jthread> threads_;
    bool stopping_ = false;
};

// Sink implementation for archive imports
class ImportQueue::StagingSink final : public MediaSink {
public:
    StagingSink(vault::Vault& v, std::string dest_gallery, uint64_t task_id,
                std::deque<StagedRecord>& records, std::mutex& mu,
                const std::shared_ptr<vault::OpProgress>& progress)
        : v_(v)
        , dest_gallery_(std::move(dest_gallery))
        , task_id_(task_id)
        , records_(records)
        , mu_(mu)
        , progress_(progress)
    {
    }

    [[nodiscard]] vault::VaultResult ensure_gallery(std::string_view rel_gallery) override
    {
        const std::string path = dest_gallery_.empty()
                                     ? std::string(rel_gallery)
                                     : dest_gallery_ + "/" + std::string(rel_gallery);
        {
            std::lock_guard lock(mu_);
            records_.emplace_back(path, std::nullopt, task_id_, false, std::string{});
        }
        return vault::VaultResult::Ok;
    }

    [[nodiscard]] vault::VaultResult tag_gallery(std::string_view rel_gallery,
                                                 std::string_view tag) override
    {
        // Deferred like every other mutation: the worker thread must not touch
        // the index tree (Phase 50), so this is queued for the main thread.
        const std::string path = dest_gallery_.empty()
                                     ? std::string(rel_gallery)
                                     : dest_gallery_ + "/" + std::string(rel_gallery);
        {
            std::lock_guard lock(mu_);
            records_.emplace_back(path, std::nullopt, task_id_, false, std::string(tag));
        }
        return vault::VaultResult::Ok;
    }

    [[nodiscard]] vault::VaultResult place_image(std::string_view rel_gallery,
                                                  std::span<const uint8_t> data,
                                                  std::string_view name) override
    {
        const std::string gallery_path = dest_gallery_.empty()
                                             ? std::string(rel_gallery)
                                             : dest_gallery_ + "/" + std::string(rel_gallery);

        // Stage the image (inline, no pool for archive imports)
        auto staged = vault::stage_image(v_, data, name, nullptr);
        if (staged.status != vault::VaultResult::Ok)
            return staged.status;

        {
            std::lock_guard lock(mu_);
            records_.emplace_back(gallery_path, std::move(staged.node), task_id_, false, std::string{});
        }
        return vault::VaultResult::Ok;
    }

    [[nodiscard]] vault::VaultResult place_video(std::string_view rel_gallery,
                                                  std::span<const uint8_t> data,
                                                  std::string_view name) override
    {
        const std::string gallery_path = dest_gallery_.empty()
                                             ? std::string(rel_gallery)
                                             : dest_gallery_ + "/" + std::string(rel_gallery);

        // Stage the video
        auto staged = vault::stage_video(v_, data, name);
        if (staged.status != vault::VaultResult::Ok)
            return staged.status;

        {
            std::lock_guard lock(mu_);
            records_.emplace_back(gallery_path, std::move(staged.node), task_id_, false, std::string{});
        }
        return vault::VaultResult::Ok;
    }

    [[nodiscard]] bool cancelled() const override
    {
        return progress_ && progress_->cancel.load();
    }

private:
    vault::Vault& v_;
    std::string dest_gallery_;
    uint64_t task_id_;
    std::deque<StagedRecord>& records_;
    std::mutex& mu_;
    std::shared_ptr<vault::OpProgress> progress_;
};

// ============================================================================
// ImportQueue implementation
// ============================================================================

ImportQueue::ImportQueue()
    : decode_pool_(std::make_unique<DecodePool>(decode_pool_size()))
{
}

ImportQueue::~ImportQueue()
{
    if (v_) {
        abort_and_flush();
    }
}

void ImportQueue::begin_session(vault::Vault& v)
{
    std::lock_guard lock(mu_);

    // Safety: a live worker at begin means end_session was skipped (e.g., via early return).
    // Loud in debug to catch caller errors.
    assert(!worker_.joinable());

    // Phase 50: reset exclusive gate — a stale true from a torn-down screen must never
    // survive into a new session (would wedge imports forever).
    exclusive_ = false;

    // Phase 50: abort idempotence is per-session; reset the flag so end_session can call
    // abort_and_flush and actually stop the worker. A stale aborted_=true would make
    // end_session a no-op, leaving the worker parked on the CV forever → jthread join hangs.
    aborted_ = false;

    // Phase 50: defense-in-depth: stale session state (tasks_ and records_) must not leak
    // into the new session. records_ point to chunks from the PREVIOUS vault's staged data —
    // attaching them into a new vault would corrupt it. abort_and_flush should have drained
    // both, but clear them here to be safe; log once if non-empty (indicates a caller bug).
    if (!records_.empty()) {
        platform::safe_println(stderr, "[ImportQueue] WARNING: begin_session with non-empty records_ (caller skipped end_session?)\n");
    }
    tasks_.clear();
    records_.clear();

    v_ = &v;
    lane_ = std::make_unique<vault::CommitLane>();
    lane_->start(v);
    v_->set_commit_router(lane_.get());
    worker_stop_ = false;
    worker_ = std::jthread([this]() { worker_loop(); });
}

void ImportQueue::end_session()
{
    abort_and_flush();
    std::lock_guard lock(mu_);
    v_ = nullptr;
}

void ImportQueue::abort_and_flush()
{
    using enum ImportTaskState;
    {
        std::lock_guard lock(mu_);

        // Make idempotent: if already aborted, this is a no-op
        if (aborted_) return;
        aborted_ = true;

        // Mark all queued tasks as cancelled and set cancel on running task
        // Also wipe any queued archive passwords (security invariant #2)
        for (auto& task : tasks_) {
            if (!task.finished()) {
                task.state = Cancelled;
            }
            if (task.progress) {
                task.progress->cancel.store(true);
            }
            // Wipe password for any queued archive task (replacement triggers old buffer's destructor wipe)
            task.password = crypto::SecureBytes{};
        }

        // Release exclusive lock and set stop flag to ensure worker can exit
        exclusive_ = false;
        worker_stop_ = true;
    }

    // Notify worker to wake from wait (either exclusive release or worker_stop)
    worker_cv_.notify_all();

    // Wait for worker to finish
    if (worker_.joinable()) {
        worker_.join();
    }

    // Drain remaining records without holding lock during vault operations
    {
        std::deque<StagedRecord> remaining;
        {
            std::lock_guard lock(mu_);
            remaining = std::move(records_);
            records_.clear();
        }

        // Process remaining records without lock
        for (auto& record : remaining) {
            if (!record.tag.empty()) {
                (void)vault::ensure_gallery_path(*v_, record.gallery_path);
                (void)v_->add_tag(record.gallery_path, record.tag);
            } else if (record.node) {
                if (!record.gallery_path.empty()) {
                    (void)vault::ensure_gallery_path(*v_, record.gallery_path);
                }
                (void)vault::attach_staged(*v_, record.gallery_path, std::move(*record.node));
            } else {
                (void)vault::ensure_gallery_path(*v_, record.gallery_path);
            }
        }
    }

    // Final commit (stop-aware: enqueue_snapshot and flush handle stopped lanes gracefully)
    if (lane_) {
        (void)lane_->enqueue_snapshot();
        (void)lane_->flush();
        lane_->stop();
    }

    // Uninstall router
    if (v_) {
        v_->set_commit_router(nullptr);
    }
}

uint64_t ImportQueue::enqueue_files(std::vector<std::filesystem::path> files,
                                    std::string dest_gallery)
{
    using enum ImportTaskState;
    std::lock_guard lock(mu_);

    const uint64_t id = next_task_id_++;
    const std::string display_name = std::format("{} files", files.size());

    Task task{
        .id = id,
        .kind = ImportTaskKind::Files,
        .display_name = display_name,
        .dest_gallery = std::move(dest_gallery),
        .files = std::move(files),
        .archive_path = {},
        .volumes = {},
        .volume_style = VolumeStyle::None,
        .volume_stem = {},
        .gallery_name = {},
        .password = {},
        .state = Queued,
        .imported = 0,
        .skipped = 0,
        .error = {},
        .progress = std::make_shared<vault::OpProgress>(),
    };
    task.progress->total.store(static_cast<int>(task.files.size()));

    tasks_.push_back(std::move(task));
    worker_cv_.notify_one();

    return id;
}

uint64_t ImportQueue::enqueue_archive(std::filesystem::path archive,
                                      std::string dest_gallery, std::string gallery_name,
                                      ImportTaskKind kind, bool password_protected,
                                      crypto::SecureBytes password)
{
    using enum ImportTaskState;
    std::lock_guard lock(mu_);

    const uint64_t id = next_task_id_++;

    Task task{
        .id = id,
        .kind = kind,
        .display_name = archive.filename().string(),
        .dest_gallery = std::move(dest_gallery),
        .files = {},
        .archive_path = std::move(archive),
        .volumes = {},
        .volume_style = VolumeStyle::None,
        .volume_stem = {},
        .gallery_name = std::move(gallery_name),
        .password = std::move(password),
        .state = Queued,
        .imported = 0,
        .skipped = 0,
        .error = {},
        .progress = std::make_shared<vault::OpProgress>(),
    };
    (void)password_protected;  // Note: used by archive executors internally

    tasks_.push_back(std::move(task));
    worker_cv_.notify_one();

    return id;
}

uint64_t ImportQueue::enqueue_volume_set(std::vector<std::filesystem::path> volumes,
                                        VolumeStyle style, std::string stem,
                                        std::string dest_gallery, std::string gallery_name,
                                        ImportTaskKind kind, crypto::SecureBytes password)
{
    using enum ImportTaskState;
    std::lock_guard lock(mu_);

    const uint64_t id = next_task_id_++;

    // Display name is the set's first volume; the status row should read like
    // the thing the user picked.
    std::filesystem::path first = volumes.empty() ? std::filesystem::path{} : volumes.front();

    Task task{
        .id = id,
        .kind = kind,
        .display_name = first.filename().string(),
        .dest_gallery = std::move(dest_gallery),
        .files = {},
        .archive_path = first,
        .volumes = std::move(volumes),
        .volume_style = style,
        .volume_stem = std::move(stem),
        .gallery_name = std::move(gallery_name),
        .password = std::move(password),
        .state = Queued,
        .imported = 0,
        .skipped = 0,
        .error = {},
        .progress = std::make_shared<vault::OpProgress>(),
    };

    tasks_.push_back(std::move(task));
    worker_cv_.notify_one();
    return id;
}

uint64_t ImportQueue::enqueue_folder(std::filesystem::path root, std::string dest_gallery,
                                     std::string gallery_name)
{
    using enum ImportTaskState;
    std::lock_guard lock(mu_);

    const uint64_t id = next_task_id_++;
    Task task{
        .id = id,
        .kind = ImportTaskKind::Folder,
        .display_name = gallery_name,
        .dest_gallery = std::move(dest_gallery),
        .files = {},
        .archive_path = std::move(root),
        .volumes = {},
        .volume_style = VolumeStyle::None,
        .volume_stem = {},
        .gallery_name = std::move(gallery_name),
        .password = {},
        .state = Queued,
        .imported = 0,
        .skipped = 0,
        .error = {},
        .progress = std::make_shared<vault::OpProgress>(),
    };
    tasks_.push_back(std::move(task));
    worker_cv_.notify_one();
    return id;
}

bool ImportQueue::cancel(uint64_t id)
{
    using enum ImportTaskState;
    std::lock_guard lock(mu_);

    for (auto& task : tasks_) {
        if (task.id != id) continue;
        if (task.state == Queued) {
            task.state = Cancelled;
            return true;
        }
        if (task.state != Running) return false;   // finished rows can't be cancelled
        if (task.progress) task.progress->cancel.store(true);
        return true;
    }
    return false;
}

bool ImportQueue::reorder(uint64_t id, int delta)
{
    std::lock_guard lock(mu_);
    std::vector<ImportTaskInfo> infos;
    for (const auto& t : tasks_) {
        infos.push_back(ImportTaskInfo{
            .id = t.id,
            .kind = t.kind,
            .display_name = t.display_name,
            .dest_gallery = t.dest_gallery,
            .state = t.state,
            .done = t.progress ? t.progress->done.load() : 0,
            .total = t.progress ? t.progress->total.load() : 0,
            .expanding = t.progress ? t.progress->expanding.load() : false,
            .imported = t.imported,
            .skipped = t.skipped,
            .error = t.error,
        });
    }

    const bool result = reorder_import_task(infos, id, delta);

    if (!result) return result;

    // Rebuild task queue in new order
    std::deque<Task> new_tasks;
    for (const auto& info : infos) {
        for (auto& t : tasks_) {
            if (t.id == info.id) {
                new_tasks.push_back(std::move(t));
                break;
            }
        }
    }
    tasks_ = std::move(new_tasks);

    return result;
}

void ImportQueue::clear_finished()
{
    std::lock_guard lock(mu_);
    auto it = tasks_.begin();
    while (it != tasks_.end()) {
        if (import_task_finished(it->state)) {
            it = tasks_.erase(it);
        } else {
            ++it;
        }
    }
}

void ImportQueue::set_exclusive(bool held)
{
    std::lock_guard lock(mu_);
    exclusive_ = held;
    if (!held) {
        // Notify the worker that the exclusive gate was released
        worker_cv_.notify_one();
    }
}

void ImportQueue::mark_task_skipped(uint64_t task_id)
{
    std::lock_guard lock(mu_);
    for (auto& t : tasks_) {
        if (t.id == task_id) {
            t.skipped++;
            break;
        }
    }
}

void ImportQueue::mark_task_imported(uint64_t task_id)
{
    std::lock_guard lock(mu_);
    for (auto& t : tasks_) {
        if (t.id == task_id) {
            t.imported++;
            break;
        }
    }
}

int ImportQueue::drain(double dt)
{
    // Collect records to process without holding the lock for vault operations
    std::deque<StagedRecord> to_process;
    {
        std::lock_guard lock(mu_);
        to_process = std::move(records_);
        records_.clear();
    }

    // Apply records without holding the lock (vault operations can be slow)
    int applied = 0;
    while (!to_process.empty()) {
        auto record = std::move(to_process.front());
        to_process.pop_front();

        if (!record.tag.empty()) {
            // Tag record: the gallery's own record was queued before it, so by
            // the time this drains the gallery exists.
            (void)vault::ensure_gallery_path(*v_, record.gallery_path);
            (void)v_->add_tag(record.gallery_path, record.tag);
        } else if (record.node) {
            // Ensure the destination gallery exists first
            if (!record.gallery_path.empty()) {
                (void)vault::ensure_gallery_path(*v_, record.gallery_path);
            }
            const auto result = vault::attach_staged(*v_, record.gallery_path, std::move(*record.node));
            if (result == vault::VaultResult::AlreadyExists) {
                mark_task_skipped(record.task_id);
            } else if (result == vault::VaultResult::Ok && !record.counted) {
                record.counted = true;
                policy_.note_attached(1);
                mark_task_imported(record.task_id);
            }
        } else {
            (void)vault::ensure_gallery_path(*v_, record.gallery_path);
        }
        applied++;
    }

    // Update policy and check conditions
    {
        std::lock_guard lock(mu_);
        // Advance batch policy
        policy_.tick(dt);

        // Check if we should commit
        if (policy_.due()) {
            (void)lane_->enqueue_snapshot();
            policy_.reset();
        }

        // Check if queue is idle
        maybe_end_batch();
    }

    return applied;
}

bool ImportQueue::busy() const
{
    std::lock_guard lock(mu_);
    // Busy only if there are unfinished tasks or pending records
    for (const auto& t : tasks_) {
        if (!t.finished()) {
            return true;
        }
    }
    return !records_.empty();
}

bool ImportQueue::lane_failed() const
{
    if (!lane_)
        return false;
    return lane_->failed();
}

std::vector<ImportTaskInfo> ImportQueue::snapshot() const
{
    std::lock_guard lock(mu_);
    std::vector<ImportTaskInfo> infos;
    for (const auto& t : tasks_) {
        infos.push_back(ImportTaskInfo{
            .id = t.id,
            .kind = t.kind,
            .display_name = t.display_name,
            .dest_gallery = t.dest_gallery,
            .state = t.state,
            .done = t.progress ? t.progress->done.load() : 0,
            .total = t.progress ? t.progress->total.load() : 0,
            .expanding = t.progress ? t.progress->expanding.load() : false,
            .imported = t.imported,
            .skipped = t.skipped,
            .error = t.error,
        });
    }
    return infos;
}

std::string ImportQueue::footer_summary() const
{
    std::lock_guard lock(mu_);
    std::vector<ImportTaskInfo> infos;
    for (const auto& t : tasks_) {
        infos.push_back(ImportTaskInfo{
            .id = t.id,
            .kind = t.kind,
            .display_name = t.display_name,
            .dest_gallery = t.dest_gallery,
            .state = t.state,
            .done = t.progress ? t.progress->done.load() : 0,
            .total = t.progress ? t.progress->total.load() : 0,
            .imported = t.imported,
            .skipped = t.skipped,
            .error = t.error,
        });
    }

    std::string error_msg;
    if (lane_ && lane_->failed()) {
        error_msg = "vault write error";
    }

    return footer_import_summary(infos, lane_ && lane_->failed(), error_msg);
}

// CALLED UNDER lock (mu_): wait for queued/running work or stop signal
bool ImportQueue::has_available_work() const
{
    using enum ImportTaskState;
    const bool stop = worker_stop_.load();
    const bool excl = exclusive_.load();

    // Check for queued/running work
    for (const auto& t : tasks_) {
        if (t.state == Queued || t.state == Running) {
            // If exclusive is held, don't wake for work; only wake if stopping
            if (excl)
                return stop;
            return true;
        }
    }
    // No work available; only wake if stopping
    return stop;
}

// CALLED UNDER lock (mu_): find and extract a queued or running task
// Returns false if none found (caller should continue), true if task extracted
bool ImportQueue::extract_task_data(uint64_t& out_task_id, ImportTaskKind& out_task_kind,
                                     std::vector<std::filesystem::path>& out_files,
                                     std::filesystem::path& out_archive_path,
                                     std::string& out_gallery_name, std::string& out_dest_gallery,
                                     std::shared_ptr<vault::OpProgress>& out_progress)
{
    using enum ImportTaskState;
    for (auto& t : tasks_) {
        if (t.state == Queued || t.state == Running) {
            // Copy all task data before releasing lock
            out_task_id = t.id;
            out_task_kind = t.kind;
            out_files = t.files;
            out_archive_path = t.archive_path;
            out_gallery_name = t.gallery_name;
            out_dest_gallery = t.dest_gallery;
            out_progress = t.progress;

            // Mark as running if it was queued
            if (t.state == Queued) {
                t.state = Running;
            }
            return true;
        }
    }
    return false;
}

// CALLED UNDER lock (mu_): update task state after processing
void ImportQueue::mark_task_complete(uint64_t task_id, const std::shared_ptr<vault::OpProgress>& progress)
{
    using enum ImportTaskState;
    for (auto& t : tasks_) {
        if (t.id == task_id) {
            if (t.state == Running) {
                // Check if the task was cancelled
                t.state = (progress && progress->cancel.load()) ? Cancelled : Done;
            }
            break;
        }
    }
}

void ImportQueue::worker_loop()
{
    while (true) {
        // Fetch task under lock, then release
        uint64_t task_id = 0;
        ImportTaskKind task_kind = ImportTaskKind::Files;
        std::vector<std::filesystem::path> task_files;
        std::filesystem::path task_archive_path;
        std::string task_gallery_name;
        std::string task_dest_gallery;
        std::shared_ptr<vault::OpProgress> task_progress;
        std::vector<std::filesystem::path> task_volumes;
        VolumeStyle                        task_volume_style = VolumeStyle::None;
        std::string                        task_volume_stem;

        {
            std::unique_lock lock(mu_);

            // Wait for a queued/running task or until worker should stop
            worker_cv_.wait(lock, [this]() { return has_available_work(); });

            if (worker_stop_)
                break;

            // Check exclusive again (just to be safe)
            if (exclusive_)
                continue;

            // Extract task data under lock
            if (!extract_task_data(task_id, task_kind, task_files, task_archive_path,
                                   task_gallery_name, task_dest_gallery, task_progress)) {
                continue;  // No task found, loop again
            }
            // Phase 53: the volume list must ride along, or a multi-volume task
            // reaches the worker with nothing to assemble and silently imports
            // its first volume as if it were a whole archive.
            for (const Task& t : tasks_) {
                if (t.id == task_id) {
                    task_volumes      = t.volumes;
                    task_volume_style = t.volume_style;
                    task_volume_stem  = t.volume_stem;
                    break;
                }
            }
        }  // Lock released here

        // Process the task using copied data (no lock held)
        Task work_task{
            .id = task_id,
            .kind = task_kind,
            .display_name = {},
            .dest_gallery = task_dest_gallery,
            .files = std::move(task_files),
            .archive_path = std::move(task_archive_path),
            .volumes = std::move(task_volumes),
            .volume_style = task_volume_style,
            .volume_stem = std::move(task_volume_stem),
            .gallery_name = std::move(task_gallery_name),
            .password = {},
            .state = ImportTaskState::Running,
            .imported = 0,
            .skipped = 0,
            .error = {},
            .progress = task_progress,
        };

        if (task_kind == ImportTaskKind::Files) {
            process_files_task(work_task);
        } else if (task_kind == ImportTaskKind::Folder) {
            process_folder_task(work_task);
        } else {
            process_archive_task(work_task);
        }

        // Update the original task with results (re-acquire lock)
        {
            std::lock_guard lock(mu_);
            mark_task_complete(task_id, task_progress);
        }
    }
}

void ImportQueue::process_files_task(Task& task)
{
    // Lookahead window for resequencing
    struct PendingFile {
        size_t index = 0;
        std::vector<uint8_t> data;
        std::shared_ptr<DecodePool::DecodeJob> decode_job;
    };

    std::deque<PendingFile> lookahead;
    size_t lookahead_bytes = 0;
    size_t file_index = 0;
    size_t next_to_stage = 0;

    const auto submit_or_inline = [&](size_t idx, const auto& file_path) {
        if (!std::filesystem::is_regular_file(file_path))
            return false;

        auto data = std::make_shared<std::vector<uint8_t>>();
        {
            std::ifstream f(file_path, std::ios::binary | std::ios::ate);
            const auto sz = f.tellg();
            if (sz <= 0 || sz > 512 * 1024 * 1024)  // 512 MiB max per file
                return false;
            data->resize(static_cast<size_t>(sz));
            f.seekg(0);
            f.read(reinterpret_cast<char*>(data->data()), sz);
        }

        if (data->empty())
            return false;

        // Detect if this is likely an image (check magic bytes)
        const bool is_image =
            (data->size() >= 2 &&
             ((*data)[0] == 0xFF && (*data)[1] == 0xD8)) ||  // JPEG
            (data->size() >= 8 && (*data)[0] == 0x89 && (*data)[1] == 0x50 &&
             (*data)[2] == 0x4E && (*data)[3] == 0x47) ||  // PNG
            (data->size() >= 12 && (*data)[8] == 0x57 && (*data)[9] == 0x45 &&
             (*data)[10] == 0x42 && (*data)[11] == 0x50);  // WebP

        PendingFile pf{.index = idx, .data = *data, .decode_job = nullptr};

        if (is_image) {
            auto job = std::make_shared<DecodePool::DecodeJob>();
            job->data = pf.data;  // Copy, don't move
            decode_pool_->submit(job);
            pf.decode_job = job;
        }

        lookahead.push_back(std::move(pf));
        lookahead_bytes += pf.data.size();

        return true;
    };

    const auto drain_lookahead = [&]() {
        // Process all lookahead items that are ready to stage (in-order by index)
        // Stop only when we encounter a gap or reach end
        while (!lookahead.empty()) {
            // Process ONLY the front item if it's the next expected index
            if (lookahead.front().index != next_to_stage)
                break;  // Wait for out-of-order items to arrive

            auto pf = std::move(lookahead.front());
            lookahead.pop_front();
            lookahead_bytes -= pf.data.size();

            if (task.progress && task.progress->cancel.load())
                return;

            if (pf.index >= task.files.size())
                return;

            const auto& file_path = task.files[pf.index];

            // Wait for decode if needed (CV-based, not spin-polling)
            if (pf.decode_job) {
                decode_pool_->wait_for_completion(pf.decode_job, 5000);  // 5 second timeout
            }

            // Stage the file
            const std::string filename = file_path.filename().string();
            const vault::StagedThumb* thumb = nullptr;
            if (pf.decode_job && decode_pool_->is_done(pf.decode_job)) {
                thumb = &pf.decode_job->result;
            }

            auto staged = vault::stage_image(*v_, pf.data, filename, thumb);
            if (staged.status == vault::VaultResult::Ok) {
                std::lock_guard lock(mu_);
                records_.push_back(StagedRecord{task.dest_gallery, std::move(staged.node),
                                               task.id, false, std::string{}});
                if (task.progress) {
                    task.progress->done.store(static_cast<int>(pf.index) + 1);
                }
            }

            next_to_stage++;
        }
    };

    // Load files into lookahead
    for (file_index = 0; file_index < task.files.size() &&
                        !(task.progress && task.progress->cancel.load());
         ++file_index) {
        const auto& file_path = task.files[file_index];

        while (lookahead.size() >= LOOKAHEAD_MAX_ITEMS ||
               lookahead_bytes >= LOOKAHEAD_MAX_BYTES) {
            const size_t size_before = lookahead.size();
            const size_t bytes_before = lookahead_bytes;
            drain_lookahead();
            // Stop if drain_lookahead made no progress (prevents infinite loop when condition blocks draining)
            if (lookahead.size() == size_before && lookahead_bytes == bytes_before) {
                break;
            }
            if (task.progress && task.progress->cancel.load())
                return;
        }

        if (!submit_or_inline(file_index, file_path)) {
            task.skipped++;
            if (task.progress) {
                task.progress->done.store(static_cast<int>(file_index) + 1);
            }
        }
    }

    // Drain remaining lookahead
    while (!lookahead.empty() && !(task.progress && task.progress->cancel.load())) {
        drain_lookahead();
    }
}

namespace {

// Merge joined spanned-ZIP volumes; they still carry per-disk offsets after a
// plain concatenation. Separated so process_volume_set_task does not nest.
[[nodiscard]] bool merge_spanned_in_place(std::vector<uint8_t>& bytes)
{
    const std::span<const uint8_t>                whole{bytes};
    const std::array<std::span<const uint8_t>, 1> one{whole};
    const auto                                    merged = merge_spanned_zip(one);
    if (merged.error != SpannedZipError::None) {
        return false;
    }
    bytes = merged.merged;
    return true;
}

} // namespace

// Assemble a detected multi-volume set and import it as ONE archive. Runs on
// the worker thread: a split set can be many GB and Phase 50 exists so the
// vault stays browsable during an import.
void ImportQueue::process_volume_set_task(Task& task, StagingSink& sink,
                                         const ArchivePassword& pw) const
{
    using enum ImportTaskState;

    VolumeSet set;
    set.style = task.volume_style;
    set.volumes.reserve(task.volumes.size());
    for (const auto& v : task.volumes) {
        set.volumes.push_back(v.filename().string());
    }

    const auto assembled = assemble_volume_set(set, task.volumes.front().parent_path());
    if (!assembled.ok()) {
        task.password.wipe();
        task.state = Failed;
        task.error = assembled.error;
        return;
    }

    ZipImportOutcome outcome;
    if (assembled.assembly == VolumeAssembly::FileOriented) {
        // Each RAR volume carries its own header, so libarchive must open the
        // files itself — there is no single buffer for the bytes-based walker.
        // FLAT by design: archives nested inside a split RAR are not recursed
        // into (v1 limitation).
        outcome = import_archive_volumes(sink, assembled.paths, task.gallery_name, "",
                                         task.progress.get(), pw);
    } else {
        std::vector<uint8_t> bytes = assembled.bytes;
        if (assembled.assembly == VolumeAssembly::SpannedZipMerge &&
            !merge_spanned_in_place(bytes)) {
            task.password.wipe();
            task.state = Failed;
            task.error = "Could not merge the spanned ZIP volumes";
            return;
        }
        // The KIND comes from the set's stem: a volume filename like
        // "whole.zip.00" has the extension ".00" and is not an archive.
        outcome = import_archive_bytes_recursive(sink, bytes, task.volume_stem, task.gallery_name,
                                                 "", task.progress.get(), pw.password);
    }

    task.password.wipe();
    if (!outcome.ok && !outcome.cancelled) {
        task.state = Failed;
        task.error = outcome.error;
    } else if (outcome.cancelled) {
        task.state = Cancelled;
    }
    task.imported = outcome.imported;
    task.skipped  = outcome.skipped;
}

void ImportQueue::process_archive_task(Task& task)
{
    using enum ImportTaskState;
    StagingSink sink(*v_, task.dest_gallery, task.id, records_, mu_, task.progress);

    // Prepare password
    ui::ArchivePassword pw{.password_protected = false, .password = {}};
    if (!task.password.empty()) {
        pw.password_protected = true;
        // Note: password is a SecureBytes, need to convert to string_view
        pw.password = std::string_view(reinterpret_cast<const char*>(task.password.data()),
                                        task.password.size());
    }

    ZipImportOutcome outcome;

    if (!task.volumes.empty()) {
        process_volume_set_task(task, sink, pw);
        return;
    }

    // Route by kind
    // Phase 53: zip/7z/rar/tar recurse into nested archives, each becoming its
    // own sub-gallery. CBZ deliberately stays on the flat importer — a comic
    // archive is a run of pages, so an archive inside one is not a chapter.
    if (task.kind == ImportTaskKind::Zip) {
        // sink_root "": StagingSink's base is dest_gallery alone, so the
        // gallery name must survive into the path rather than being stripped.
        outcome = import_archive_recursive(sink, task.archive_path, task.gallery_name, "",
                                           task.progress.get(), pw.password);
    } else if (task.kind == ImportTaskKind::Cbz) {
        outcome = import_cbz(sink, task.archive_path, task.gallery_name, "", task.progress.get());
    } else if (task.kind == ImportTaskKind::Archive) {
        outcome = import_archive_recursive(sink, task.archive_path, task.gallery_name, "",
                                           task.progress.get(), pw.password);
    } else if (task.kind == ImportTaskKind::ArchiveCbz) {
        outcome =
            import_archive_cbz(sink, task.archive_path, task.gallery_name, "", task.progress.get(), pw);
    }

    // Wipe password
    task.password.wipe();

    if (outcome.needs_password) {
        task.state = Failed;
        task.error = "Wrong or missing archive password";
    } else if (!outcome.ok && !outcome.cancelled) {
        task.state = Failed;
        task.error = outcome.error;
    } else if (outcome.cancelled) {
        task.state = Cancelled;
    }

    task.imported = outcome.imported;
    task.skipped = outcome.skipped;
}

void ImportQueue::process_folder_task(Task& task)
{
    using enum ImportTaskState;
    StagingSink sink(*v_, task.dest_gallery, task.id, records_, mu_, task.progress);

    // Scan the folder
    const auto entries = scan_folder(task.archive_path);

    // Base "" — NOT dest_gallery. StagingSink already prefixes dest_gallery, so
    // building absolute paths here applied it twice: a folder imported into
    // "Parent" landed under "Parent/Parent/...". At the vault root the two
    // cancelled out, which is why it went unnoticed.
    const auto plan = build_zip_plan(entries, "", task.gallery_name);

    // Set total for progress tracking
    if (task.progress) {
        task.progress->total.store(static_cast<int>(plan.placements.size()));
    }

    // Create galleries in order (parents first)
    for (const auto& gallery : plan.galleries) {
        if (task.progress && task.progress->cancel.load())
            return;
        (void)sink.ensure_gallery(gallery);
    }

    // Process each placement
    for (size_t i = 0; i < plan.placements.size(); ++i) {
        if (task.progress && task.progress->cancel.load()) {
            return;
        }
        const auto& placement = plan.placements[i];
        const auto& entry = entries[placement.entry_index];
        if (entry.is_dir) {
            continue;  // Skip directories (already handled by ensure_gallery)
        }
        place_folder_file(sink, placement, entry, task, i);
    }

    task.skipped += plan.skipped_unsupported;
}

void ImportQueue::place_folder_file(StagingSink& sink, const ZipPlacement& placement,
                                    const ZipEntry& entry, Task& task, size_t index) const
{
    const auto mark_done = [&] {
        if (task.progress) {
            task.progress->done.store(static_cast<int>(index) + 1);
        }
    };

    const auto file_path = task.archive_path / entry.path;

    // Read file into mlock'd buffer (security invariant: no swappable memory for decrypted data)
    crypto::SecureBytes file_data;
    if (!try_read_file(file_path, file_data)) {
        task.skipped++;
        mark_done();
        return;
    }

    // Place the file based on extension.
    vault::VaultResult result = vault::VaultResult::Ok;
    if (is_supported_image_name(placement.filename)) {
        result = sink.place_image(placement.gallery_path, file_data.as_span(), placement.filename);
    } else if (is_supported_media_name(placement.filename)) {
        result = sink.place_video(placement.gallery_path, file_data.as_span(), placement.filename);
    } else {
        task.skipped++;
        mark_done();
        return;
    }

    if (result != vault::VaultResult::Ok) {
        task.skipped++;
    }
    mark_done();
}

void ImportQueue::maybe_end_batch()
{
    // Check if fully idle: no tasks, no records, lane idle
    if (!tasks_.empty() || !records_.empty() || (lane_ && !lane_->idle())) {
        return;
    }

    // Queue is idle: enqueue final snapshot, flush, and uninstall router
    if (lane_) {
        (void)lane_->enqueue_snapshot();
        (void)lane_->flush();
    }

    if (v_) {
        v_->set_commit_router(nullptr);
    }
}

// Test seam: joins the worker WITHOUT the final lane flush, simulating a crash
// between batch commits. Never called by production code.
void test_only_drop_without_flush(ImportQueue& q)
{
    {
        std::lock_guard lock(q.mu_);

        // Mark all queued tasks as cancelled (same as abort_and_flush)
        for (auto& task : q.tasks_) {
            if (!task.finished()) {
                task.state = ImportTaskState::Cancelled;
            }
            if (task.progress) {
                task.progress->cancel.store(true);
            }
            // Wipe password for any queued archive task
            task.password = crypto::SecureBytes{};
        }

        // Release exclusive lock and set stop flag
        q.exclusive_ = false;
        q.worker_stop_ = true;
    }

    // Notify worker to wake
    q.worker_cv_.notify_all();

    // Wait for worker to finish
    if (q.worker_.joinable()) {
        q.worker_.join();
    }

    // Apply remaining records to the vault (same as abort_and_flush)
    {
        std::deque<ImportQueue::StagedRecord> remaining;
        {
            std::lock_guard lock(q.mu_);
            remaining = std::move(q.records_);
            q.records_.clear();
        }

        // Process remaining records
        for (auto& record : remaining) {
            if (!record.tag.empty()) {
                (void)vault::ensure_gallery_path(*q.v_, record.gallery_path);
                (void)q.v_->add_tag(record.gallery_path, record.tag);
            } else if (record.node) {
                if (!record.gallery_path.empty()) {
                    (void)vault::ensure_gallery_path(*q.v_, record.gallery_path);
                }
                (void)vault::attach_staged(*q.v_, record.gallery_path, std::move(*record.node));
            } else {
                (void)vault::ensure_gallery_path(*q.v_, record.gallery_path);
            }
        }
    }

    // Uninstall router (but do NOT flush the lane — this simulates a crash)
    if (q.v_) {
        q.v_->set_commit_router(nullptr);
    }

    // Stop the lane without flushing (the lane worker may still be in flight
    // or have pending writes, but we abandon them here to simulate the crash)
    if (q.lane_) {
        // Just stop; don't enqueue_snapshot or flush
        q.lane_->stop();
    }
}

}  // namespace ui
