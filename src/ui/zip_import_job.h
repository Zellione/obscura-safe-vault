#pragma once

#include "ui/zip_import.h"

#include <atomic>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>

namespace vault {
class Vault;
}

namespace ui {

// Runs a ZIP/CBZ import on a background thread so the UI never freezes on a
// many-page comic (Phase 24 fix — a 180-page volume is ~10 s of decode + encrypt).
//
// Threading contract: the vault's file handle is single-threaded (see
// image::DecodeWorker) — while active(), the worker thread is the ONLY thread
// touching the vault. The owning screen MUST NOT read the vault (no thumbnail
// decrypt, no listing) until take_outcome() returns; it only polls progress and
// draws a modal. cancel() cooperatively stops between pages (append-only vault →
// a clean partial import).
class ZipImportJob {
public:
    ZipImportJob() = default;
    ~ZipImportJob();

    ZipImportJob(const ZipImportJob&)            = delete;
    ZipImportJob& operator=(const ZipImportJob&) = delete;

    // Launch a CBZ import into `v` (kept alive by the caller for the job's life).
    // Returns false if a job is already active (one at a time).
    bool start_cbz(vault::Vault& v, std::filesystem::path cbz,
                   std::string base_gallery, std::string gallery_name);

    // True from start_cbz() until take_outcome() has collected the result.
    [[nodiscard]] bool active() const noexcept { return active_.load(); }

    // Live counters for a progress bar (valid while active()).
    [[nodiscard]] int total() const noexcept { return progress_.total.load(); }
    [[nodiscard]] int done()  const noexcept { return progress_.done.load(); }

    // Request a cooperative stop; pages stored so far remain.
    void cancel() noexcept { progress_.cancel.store(true); }

    // If the worker has finished, join it and hand back the outcome exactly once;
    // returns nullopt while still running or when there is nothing to collect.
    [[nodiscard]] std::optional<ZipImportOutcome> take_outcome();

private:
    ImportProgress    progress_;
    std::atomic<bool> active_{false};   // job in flight or finished-but-uncollected
    std::atomic<bool> done_{false};     // worker set this at the end
    ZipImportOutcome  outcome_;         // written by worker; read after join
    std::thread       thread_;
};

}  // namespace ui
