#pragma once

// Phase 50: Asynchronous index-commit lane. One consumer thread persists
// full-snapshot index blobs in generation order, coalescing: only the newest
// pending blob is ever written (each blob is a complete snapshot, so skipping
// stale ones is always safe). All disk work happens under the vault's write
// mutex via index_io::commit_plain_blob.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace vault {

class Vault;

class CommitLane {
public:
    CommitLane() = default;
    ~CommitLane();                       // stop() if running
    CommitLane(const CommitLane&)            = delete;
    CommitLane& operator=(const CommitLane&) = delete;

    // Bind to an UNLOCKED vault and start the lane thread. `v` must outlive
    // the lane's stop(). One lane per vault session. The Vault's address must remain
    // stable (no moves) while the lane is active; App holds the vault behind a
    // unique_ptr and never moves it while a CommitLane is bound.
    void start(Vault& v);

    // MAIN THREAD: serialize the tree NOW (cheap, memory-only) and hand the
    // blob to the lane, replacing any not-yet-written pending blob. Returns
    // false if serialization failed or the lane has failed (failed() true).
    [[nodiscard]] bool enqueue_snapshot();

    // Block until the pending blob (if any) is durably committed. Returns
    // false if the lane has failed. Safe to call with nothing pending.
    [[nodiscard]] bool flush();

    // A disk write failed; the lane refuses further work (hard stop per spec).
    [[nodiscard]] bool failed() const noexcept;

    // flush() then join the thread. Idempotent. Noexcept: swallows all failures
    // into the failed() flag rather than throwing, safe to call from noexcept contexts
    // like Vault::lock().
    void stop() noexcept;

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] bool idle() const;     // nothing pending or in flight

private:
    struct Pending {
        std::vector<uint8_t> plain;
        uint64_t generation = 0;
    };

    void worker_loop();

    Vault* v_ = nullptr;

    mutable std::mutex mu_;
    mutable std::condition_variable cv_;
    std::optional<Pending> pending_;
    uint64_t next_gen_ = 1;
    uint64_t last_written_gen_ = 0;
    bool in_flight_ = false;
    std::atomic<bool> failed_{false};
    std::atomic<bool> stopping_{false};
    std::jthread thread_;
};

}  // namespace vault
