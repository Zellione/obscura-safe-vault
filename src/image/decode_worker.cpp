#include "image/decode_worker.h"

#include <algorithm>
#include <utility>

#include "image/decode.h"

namespace image {

uint32_t decode_wake_event()
{
    static const uint32_t ev = [] {
        const uint32_t e = SDL_RegisterEvents(1);
        return e == static_cast<uint32_t>(-1) ? 0u : e;
    }();
    return ev;
}

DecodeWorker::DecodeWorker(uint32_t wake_event)
    : wake_event_(wake_event)
{
    thread_ = std::jthread([this] { run(); });
}

DecodeWorker::~DecodeWorker()
{
    {
        std::lock_guard lock(mtx_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    // Remaining queued/done items wipe their SecureBytes on destruction here.
}

void DecodeWorker::submit(uint64_t key, crypto::SecureBytes&& encoded)
{
    {
        std::lock_guard lock(mtx_);
        if (!inflight_.insert(key).second) return;   // already queued / decoding
        queue_.emplace_back(key, std::move(encoded));
    }
    cv_.notify_one();
}

bool DecodeWorker::pending(uint64_t key) const
{
    std::lock_guard lock(mtx_);
    return inflight_.contains(key);
}

std::optional<DecodeWorker::Result> DecodeWorker::take_result()
{
    std::lock_guard lock(mtx_);
    if (done_.empty()) return std::nullopt;
    Result r = std::move(done_.back());
    done_.pop_back();
    inflight_.erase(r.key);
    return r;
}

void DecodeWorker::retain(std::span<const uint64_t> keep)
{
    std::lock_guard lock(mtx_);
    std::erase_if(queue_, [&keep, this](const Job& j) {
        if (std::ranges::find(keep, j.key) != keep.end()) return false;
        inflight_.erase(j.key);   // queued-only items can be safely forgotten
        return true;
    });
}

void DecodeWorker::run()
{
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mtx_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty()) return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }

        // Decode outside the lock — this is the slow part. `job.encoded` (the
        // mlock'd compressed plaintext) is wiped when `job` goes out of scope.
        std::optional<ImageData> img = decode_from_memory(job.encoded.as_span());

        {
            std::lock_guard lock(mtx_);
            done_.emplace_back(job.key, std::move(img));
        }
        if (wake_event_ != 0) {
            SDL_Event e{};
            e.type = wake_event_;
            SDL_PushEvent(&e);   // thread-safe; wakes an event-driven loop
        }
    }
}

} // namespace image
