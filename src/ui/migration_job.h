#pragma once

// Phase 65: the one-time blocking vault migration.
//
// Threading contract (FileOpJob's, NOT the Phase 50 staging contract): while
// active(), this job owns the vault EXCLUSIVELY. The owning screen must not
// read the vault — no thumbnail decrypt, no listing — until take_outcome()
// returns; it only polls progress and draws a modal. That exclusivity is what
// lets the coordinator mutate the index tree directly, which a background
// import (running concurrently with browsing) may never do.
//
// Cancel stops between items. Work applied so far is committed and durable, but
// the watermark is NOT stamped and compaction is skipped, so the migration is
// re-offered at the next unlock.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "vault/op_progress.h"

namespace vault { class Vault; }

namespace ui {

// Coarse stage, for the modal's label. Progress counters are per-stage.
enum class MigrationPhase { Idle, Scanning, Repairing, Committing, Compacting, Done };

// The progress modal's title + "done / total" line for a given phase — pure
// and independent of rendering, so it's unit-testable without a gfx::Renderer.
// draw_migration_progress (src/app/app.cpp) is the sole caller.
struct MigrationProgressText {
    std::string title;
    std::string count_line;
};

// `done`/`total` are the job's current progress counters (job.done()/total()).
[[nodiscard]] MigrationProgressText migration_progress_text(MigrationPhase phase, int done,
                                                             int total);

struct MigrationOutcome {
    bool        ok        = false;  // ran to completion (or a clean cancel)
    bool        cancelled = false;
    int         videos_fixed   = 0; // codec resolved and metadata written
    int         videos_skipped = 0; // still undecodable — watermark still advances
    int         images_fixed   = 0; // animated flag corrected
    int         thumbs_fixed   = 0; // Phase 75: thumbnails/posters regenerated at 512px
    int         failed         = 0; // read/decrypt failures
    int         total          = 0; // items attempted
    uint64_t    reclaimed_bytes = 0;// freed by the compaction phase
    std::string status;             // human-facing summary (never any content)
    std::string error;              // set when ok == false
};

class MigrationJob {
public:
    MigrationJob() = default;
    ~MigrationJob();

    MigrationJob(const MigrationJob&)            = delete;
    MigrationJob& operator=(const MigrationJob&) = delete;

    // Spawn the coordinator. False if a job is already in flight. `v` must
    // outlive the job and must not be touched by anyone else until
    // take_outcome() returns.
    bool start(vault::Vault& v);

    [[nodiscard]] bool active() const noexcept { return active_.load(); }
    [[nodiscard]] int  total()  const noexcept { return progress_.total.load(); }
    [[nodiscard]] int  done()   const noexcept { return progress_.done.load(); }
    [[nodiscard]] MigrationPhase phase() const noexcept { return phase_.load(); }

    void cancel() noexcept { progress_.cancel.store(true); }

    // Join and hand back the outcome exactly once; nullopt while still running.
    [[nodiscard]] std::optional<MigrationOutcome> take_outcome();

    // Phase 79: synchronous teardown for App::shutdown — cancel, join the
    // coordinator, and deactivate WITHOUT a take_outcome() poll (the outcome is
    // discarded). The caller may only lock/destroy the Vault after this returns;
    // tearing the vault down under a live coordinator is a use-after-free.
    // Cancel semantics are the normal ones: applied work is committed, the
    // watermark is not stamped, so the upgrade is re-offered at the next unlock.
    void abort_and_join();

private:
    void run(vault::Vault& v);

    vault::OpProgress            progress_;
    std::atomic<bool>            active_{false};
    std::atomic<bool>            done_{false};
    std::atomic<MigrationPhase>  phase_{MigrationPhase::Idle};
    MigrationOutcome             outcome_;   // written by worker, read after join
    std::jthread                 thread_;
};

} // namespace ui
