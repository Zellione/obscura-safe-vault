#pragma once

#include <atomic>

namespace vault {

// Thread-safe progress + cooperative-cancel handle shared between a long-running
// bulk vault operation on a worker thread and a UI poller (Phase 25). The worker
// stores `total` (item count) before the first item and bumps `done` after each;
// the poller reads them for an "N / M" progress bar. Setting `cancel` asks the
// worker to stop between items — because every underlying step is a committed,
// crash-safe vault mutation (append-only add / atomic index swap), a cancel is
// always a clean partial result, never a corrupt one.
//
// Lives in vault/ (not ui/) so vault-level bulk ops (transfer, export helpers)
// can report progress without a ui dependency. FileOpJob (Phase 25) and the
// queue import workers share this type.
struct OpProgress {
    std::atomic<int>  total{0};
    std::atomic<int>  done{0};
    std::atomic<bool> cancel{false};
};

} // namespace vault
