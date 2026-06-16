#pragma once

#include <SDL3/SDL.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <unordered_set>
#include <vector>

#include "crypto/secure_mem.h"
#include "image/image.h"

namespace image {

// Lazily register (once) and return the SDL event type used to wake the app's
// event loop when an off-thread decode finishes. Returns 0 if registration
// fails (the loop then relies on its idle heartbeat). Must be called after
// SDL_Init; safe to call from multiple screens (the registration is memoised).
[[nodiscard]] uint32_t decode_wake_event();

// Off-thread image decoder so the UI never stalls on a slow decode.
//
// Decryption stays on the caller's (render) thread — it is fast and touches the
// vault's single stateful file handle, which must not be shared across threads.
// Only the expensive part, decode_from_memory() (JPEG/PNG/HEIC decompression,
// tens to hundreds of ms for a large photo), is moved onto one background thread.
//
// Flow per image: the caller reads+decrypts the stored bytes into an mlock'd
// SecureBytes and submit()s them keyed by the chunk's data_offset; the worker
// decodes and queues an ImageData; the caller drains finished decodes with
// take_result() and uploads them to the GPU on its own thread. The compressed
// source bytes are wiped (SecureBytes destructor) the moment the decode finishes,
// so plaintext never outlives the decode and never reaches disk (invariant #1).
class DecodeWorker {
public:
    struct Result {
        uint64_t                 key   = 0;
        std::optional<ImageData> image;   // empty when the decode failed
    };

    // `wake_event` is an SDL event type (from SDL_RegisterEvents) pushed whenever
    // a decode finishes, so an event-driven loop wakes up to drain the result.
    // Pass 0 to disable the wake (e.g. in headless tests).
    explicit DecodeWorker(uint32_t wake_event = 0);
    ~DecodeWorker();

    DecodeWorker(const DecodeWorker&)            = delete;
    DecodeWorker& operator=(const DecodeWorker&) = delete;

    // Queue `encoded` (decrypted, still-compressed image bytes) for decode under
    // `key`. Coalesced: a no-op if `key` is already queued or in flight. Takes
    // ownership of `encoded`.
    void submit(uint64_t key, crypto::SecureBytes&& encoded);

    // True while `key` is queued or being decoded (its result not yet taken).
    [[nodiscard]] bool pending(uint64_t key) const;

    // Pop one finished decode, or nullopt if none are ready. Non-blocking.
    [[nodiscard]] std::optional<Result> take_result();

    // Drop still-queued requests whose key is absent from `keep`; items already
    // decoding or finished are untouched. Keeps the worker from decoding images
    // that have scrolled out of view.
    void retain(std::span<const uint64_t> keep);

private:
    struct Job {
        uint64_t            key = 0;
        crypto::SecureBytes encoded;
    };

    void run();

    mutable std::mutex           mtx_;
    std::condition_variable      cv_;
    std::deque<Job>              queue_;
    std::vector<Result>          done_;
    std::unordered_set<uint64_t> inflight_;   // submitted, result not yet taken
    bool                         stop_       = false;
    uint32_t                     wake_event_ = 0;
    // std::thread (not jthread): jthread is absent from AppleClang's libc++. The
    // destructor explicitly signals stop + joins, so the RAII guarantee holds.
    std::thread                  thread_;
};

} // namespace image
