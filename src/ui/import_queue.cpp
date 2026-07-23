#include "ui/import_queue.h"
#include "ui/archive_import.h"
#include "ui/media_sink.h"
#include "ui/zip_import.h"
#include "image/decode.h"
#include "image/gif_info.h"
#include "image/thumbnail.h"
#include "vault/staging.h"

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
}

// ============================================================================
// Internal structures
// ============================================================================

struct ImportQueue::StagedRecord {
    std::string gallery_path;
    std::optional<vault::IndexNode> node;  // nullopt => ensure gallery only
    uint64_t task_id = 0;
    bool counted = false;
};

struct ImportQueue::Task {
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
        cv_.notify_one();
    }

    [[nodiscard]] bool is_done(const std::shared_ptr<DecodeJob>& job) const
    {
        return job->done.load();
    }

private:
    void worker_loop()
    {
        while (true) {
            std::unique_lock lock(mu_);
            cv_.wait(lock, [this]() { return !queue_.empty() || stopping_; });

            if (stopping_ && queue_.empty())
                break;

            if (queue_.empty())
                continue;

            auto job = queue_.front();
            queue_.pop();
            lock.unlock();

            // Decode outside the lock
            if (auto decoded = image::decode_from_memory(job->data)) {
                job->result.width = decoded->width;
                job->result.height = decoded->height;
                job->result.format = static_cast<vault::ImageFormat>(decoded->format);

                // Make thumbnail
                if (auto thumb_bytes = image::make_thumbnail(*decoded, 256, 85)) {
                    job->result.thumb_jpeg = std::move(*thumb_bytes);
                }

                // Set animated flag for GIFs
                if (decoded->format == image::ImageFormat::GIF) {
                    job->result.animated = image::gif_is_animated(job->data);
                }
            }
            job->data.clear();  // Free the source pixels
            job->done.store(true);
        }
    }

    mutable std::mutex mu_;
    std::condition_variable cv_;
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
            records_.push_back(StagedRecord{path, std::nullopt, task_id_, false});
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
            records_.push_back(StagedRecord{gallery_path, std::move(staged.node), task_id_, false});
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
            records_.push_back(StagedRecord{gallery_path, std::move(staged.node), task_id_, false});
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
        fprintf(stderr, "[ImportQueue] WARNING: begin_session with non-empty records_ (caller skipped end_session?)\n");
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
    {
        std::lock_guard lock(mu_);

        // Make idempotent: if already aborted, this is a no-op
        if (aborted_) return;
        aborted_ = true;

        // Mark all queued tasks as cancelled and set cancel on running task
        // Also wipe any queued archive passwords (security invariant #2)
        for (auto& task : tasks_) {
            if (!task.finished()) {
                task.state = ImportTaskState::Cancelled;
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
            if (record.node) {
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
    std::lock_guard lock(mu_);

    const uint64_t id = next_task_id_++;
    const std::string display_name = std::to_string(files.size()) + " files";

    Task task{
        .id = id,
        .kind = ImportTaskKind::Files,
        .display_name = display_name,
        .dest_gallery = std::move(dest_gallery),
        .files = std::move(files),
        .archive_path = {},
        .gallery_name = {},
        .password = {},
        .state = ImportTaskState::Queued,
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
    std::lock_guard lock(mu_);

    const uint64_t id = next_task_id_++;

    Task task{
        .id = id,
        .kind = kind,
        .display_name = archive.filename().string(),
        .dest_gallery = std::move(dest_gallery),
        .files = {},
        .archive_path = std::move(archive),
        .gallery_name = std::move(gallery_name),
        .password = std::move(password),
        .state = ImportTaskState::Queued,
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

bool ImportQueue::cancel(uint64_t id)
{
    std::lock_guard lock(mu_);

    for (auto& task : tasks_) {
        if (task.id == id) {
            if (task.state == ImportTaskState::Queued) {
                task.state = ImportTaskState::Cancelled;
                return true;
            } else if (task.state == ImportTaskState::Running) {
                if (task.progress) {
                    task.progress->cancel.store(true);
                }
                return true;
            }
            return false;
        }
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
            .imported = t.imported,
            .skipped = t.skipped,
            .error = t.error,
        });
    }

    const bool result = reorder_import_task(infos, id, delta);

    if (result) {
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
    }

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

        if (record.node) {
            // Ensure the destination gallery exists first
            if (!record.gallery_path.empty()) {
                (void)vault::ensure_gallery_path(*v_, record.gallery_path);
            }
            const auto result = vault::attach_staged(*v_, record.gallery_path, std::move(*record.node));
            if (result == vault::VaultResult::AlreadyExists) {
                // Find the task and increment skipped (re-acquire lock for update)
                {
                    std::lock_guard lock(mu_);
                    for (auto& t : tasks_) {
                        if (t.id == record.task_id) {
                            t.skipped++;
                            break;
                        }
                    }
                }
            } else if (result == vault::VaultResult::Ok) {
                if (!record.counted) {
                    record.counted = true;
                    policy_.note_attached(1);
                    // Update task imported count (re-acquire lock)
                    {
                        std::lock_guard lock(mu_);
                        for (auto& t : tasks_) {
                            if (t.id == record.task_id) {
                                t.imported++;
                                break;
                            }
                        }
                    }
                }
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

void ImportQueue::worker_loop()
{
    while (true) {
        // Store task data locally to avoid holding references across lock release
        uint64_t task_id = 0;
        ImportTaskKind task_kind = ImportTaskKind::Files;
        std::vector<std::filesystem::path> task_files;
        std::filesystem::path task_archive_path;
        std::string task_gallery_name, task_dest_gallery;
        std::shared_ptr<vault::OpProgress> task_progress;
        bool found_task = false;

        {
            std::unique_lock lock(mu_);

            // Wait for a queued/running task or until worker should stop
            worker_cv_.wait(lock, [this]() {
                const bool stop = worker_stop_.load(std::memory_order_acquire);
                const bool excl = exclusive_.load(std::memory_order_acquire);

                // Check for queued/running work
                for (const auto& t : tasks_) {
                    if (t.state == ImportTaskState::Queued ||
                        t.state == ImportTaskState::Running) {
                        // If exclusive is held, don't wake for work; only wake if stopping
                        if (excl)
                            return stop;
                        return true;
                    }
                }
                // No work available; only wake if stopping
                return stop;
            });

            if (worker_stop_)
                break;

            // Check exclusive again (just to be safe)
            if (exclusive_)
                continue;

            // Find the first queued or running task and copy all its data
            for (auto& t : tasks_) {
                if (t.state == ImportTaskState::Queued) {
                    // Copy all task data before releasing lock
                    task_id = t.id;
                    task_kind = t.kind;
                    task_files = t.files;
                    task_archive_path = t.archive_path;
                    task_gallery_name = t.gallery_name;
                    task_dest_gallery = t.dest_gallery;
                    task_progress = t.progress;
                    found_task = true;

                    // Mark as running
                    t.state = ImportTaskState::Running;
                    break;
                } else if (t.state == ImportTaskState::Running) {
                    // Continue processing this one - copy all task data
                    task_id = t.id;
                    task_kind = t.kind;
                    task_files = t.files;
                    task_archive_path = t.archive_path;
                    task_gallery_name = t.gallery_name;
                    task_dest_gallery = t.dest_gallery;
                    task_progress = t.progress;
                    found_task = true;
                    break;
                }
            }

            if (!found_task)
                continue;
        }

        // Process the task using copied data (no lock held, no task references)
        Task work_task{
            .id = task_id,
            .kind = task_kind,
            .display_name = {},
            .dest_gallery = task_dest_gallery,
            .files = std::move(task_files),
            .archive_path = std::move(task_archive_path),
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
        } else {
            process_archive_task(work_task);
        }

        // Update the original task with results, by ID lookup
        {
            std::lock_guard lock(mu_);
            for (auto& t : tasks_) {
                if (t.id == task_id) {
                    if (t.state == ImportTaskState::Running) {
                        // Check if the task was cancelled
                        if (task_progress && task_progress->cancel.load()) {
                            t.state = ImportTaskState::Cancelled;
                        } else {
                            t.state = ImportTaskState::Done;
                        }
                    }
                    break;
                }
            }
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
        while (!lookahead.empty() && next_to_stage < lookahead.front().index + 1) {
            auto pf = std::move(lookahead.front());
            lookahead.pop_front();
            lookahead_bytes -= pf.data.size();

            if (task.progress && task.progress->cancel.load())
                return;

            if (pf.index >= task.files.size())
                return;

            const auto& file_path = task.files[pf.index];

            // Wait for decode if needed
            if (pf.decode_job) {
                // Spin-wait for the decode job
                int wait_count = 0;
                while (!decode_pool_->is_done(pf.decode_job) && wait_count < 10000) {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                    wait_count++;
                }
            }

            // Stage the file
            const std::string filename = file_path.filename().string();
            const vault::StagedThumb* thumb = nullptr;
            if (pf.decode_job && decode_pool_->is_done(pf.decode_job)) {
                thumb = &pf.decode_job->result;
            }

            const auto staged = vault::stage_image(*v_, pf.data, filename, thumb);
            if (staged.status == vault::VaultResult::Ok) {
                std::lock_guard lock(mu_);
                records_.push_back(StagedRecord{task.dest_gallery, std::move(staged.node),
                                               task.id, false});
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
            drain_lookahead();
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

void ImportQueue::process_archive_task(Task& task)
{
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

    // Route by kind
    if (task.kind == ImportTaskKind::Zip) {
        outcome = import_zip(sink, task.archive_path, task.gallery_name, task.progress.get());
    } else if (task.kind == ImportTaskKind::Cbz) {
        outcome = import_cbz(sink, task.archive_path, task.gallery_name, task.progress.get());
    } else if (task.kind == ImportTaskKind::Archive) {
        outcome = import_archive(sink, task.archive_path, task.gallery_name, task.progress.get(), pw);
    } else if (task.kind == ImportTaskKind::ArchiveCbz) {
        outcome =
            import_archive_cbz(sink, task.archive_path, task.gallery_name, task.progress.get(), pw);
    }

    // Wipe password
    task.password.wipe();

    if (outcome.needs_password) {
        task.state = ImportTaskState::Failed;
        task.error = "Wrong or missing archive password";
    } else if (!outcome.ok && !outcome.cancelled) {
        task.state = ImportTaskState::Failed;
        task.error = outcome.error;
    } else if (outcome.cancelled) {
        task.state = ImportTaskState::Cancelled;
    }

    task.imported = outcome.imported;
    task.skipped = outcome.skipped;
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

}  // namespace ui
