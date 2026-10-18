#pragma once

#include "ui/import_queue.h"

#include <chrono>
#include <thread>

namespace importtest {

// Use elapsed time, not an iteration budget: drain may perform synchronous SQL I/O.
// Callers must REQUIRE success before interpreting outcomes or starting teardown.
[[nodiscard]] inline bool
drain_until_idle(ui::ImportQueue& queue,
                 std::chrono::milliseconds timeout = std::chrono::seconds(30))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        (void)queue.drain(0.001);
        if (!queue.busy()) {
            (void)queue.drain(0.001);
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

}  // namespace importtest
