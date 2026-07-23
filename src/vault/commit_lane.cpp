#include "commit_lane.h"

#include <cassert>

#include "vault.h"
#include "index_io.h"
#include "platform/error_log.h"

namespace vault {

CommitLane::~CommitLane()
{
    stop();
}

void CommitLane::start(Vault& v)
{
    assert(!running() && "CommitLane already running");
    v_ = &v;
    thread_ = std::jthread(&CommitLane::worker_loop, this);
}

bool CommitLane::enqueue_snapshot()
{
    if (!v_) return false;
    if (failed()) return false;

    // Serialize the index tree (main-thread-only, no lock needed for tree).
    // This happens OUTSIDE mu_ since the tree is not protected by header_mutex_.
    IndexIoContext ctx{
        .fp_           = v_->fp_,
        .header_       = v_->header_,
        .master_key_   = v_->master_key_,
        .root_         = v_->root_,
        .saved_searches_ = v_->saved_searches_,
        .settings_     = v_->settings_,
        .header_mutex_ = v_->header_mutex_.get(),
    };

    std::vector<uint8_t> blob;
    if (!index_io::serialize_plain_index(ctx, blob)) {
        return false;
    }

    // Take the lock and enqueue the blob, replacing any not-yet-written pending blob.
    {
        std::lock_guard lk(mu_);
        pending_ = Pending{.plain = std::move(blob), .generation = next_gen_++};
        cv_.notify_all();
    }

    return true;
}

bool CommitLane::flush()
{
    std::unique_lock lk(mu_);
    cv_.wait(lk, [this] { return (!pending_ && !in_flight_) || failed(); });
    return !failed();
}

bool CommitLane::failed() const noexcept
{
    return failed_.load(std::memory_order_acquire);
}

void CommitLane::stop()
{
    if (!running()) return;

    // Signal the worker to stop.
    {
        std::lock_guard lk(mu_);
        stopping_.store(true, std::memory_order_release);
        cv_.notify_all();
    }

    // Wait for the thread to finish (jthread joins automatically on destruction,
    // but we explicitly call it here for clarity).
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool CommitLane::running() const noexcept
{
    return thread_.joinable();
}

bool CommitLane::idle() const
{
    std::lock_guard lk(mu_);
    return !pending_ && !in_flight_;
}

void CommitLane::worker_loop()
{
    while (true) {
        std::optional<Pending> work;

        // Wait for work or shutdown signal.
        {
            std::unique_lock lk(mu_);
            cv_.wait(lk, [this] { return pending_ || stopping_.load(std::memory_order_acquire); });

            // Check if we should exit: stopping && no pending work.
            if (stopping_.load(std::memory_order_acquire) && !pending_) {
                break;
            }

            // Take the pending work if it exists.
            if (pending_) {
                work = std::move(*pending_);
                pending_.reset();
                in_flight_ = true;
            }
        }

        // Process the work outside the lock.
        if (work) {
            // Build IndexIoContext for the commit operation.
            IndexIoContext ctx{
                .fp_           = v_->fp_,
                .header_       = v_->header_,
                .master_key_   = v_->master_key_,
                .root_         = v_->root_,
                .saved_searches_ = v_->saved_searches_,
                .settings_     = v_->settings_,
                .header_mutex_ = v_->header_mutex_.get(),
            };

            // Acquire the vault write mutex and commit the blob.
            {
                std::lock_guard wl(*v_->write_mutex_);
                if (index_io::commit_plain_blob(ctx, work->plain) != VaultResult::Ok) {
                    failed_.store(true, std::memory_order_release);
                    platform::log_error("Vault", "commit lane: index write failed");
                }
            }

            // Mark the work as done and notify waiters.
            {
                std::lock_guard lk(mu_);
                in_flight_ = false;
                last_written_gen_ = work->generation;
                cv_.notify_all();
            }
        }
    }
}

}  // namespace vault
