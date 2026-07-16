#pragma once

#include "crypto/secure_mem.h"
#include "ui/zip_import.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <thread>

namespace vault {
class Vault;
}

namespace ui {

// Groups the mirror/append destination + conflict policy (Phase 34) — both
// trivial enum values — to bring start_archive under the S107 parameter-count
// cap. Deliberately NOT used to bundle the Phase 35 password: SonarQube's
// dataflow analysis lost track of ownership when crypto::SecureBytes (which
// owns a std::unique_ptr internally) was nested inside an aggregate passed
// by value into start_archive/start_archive_cbz and reported a false-positive
// leak (cpp:S3584) at the gallery_grid.cpp call site, so password_protected/
// password stay flat parameters here, matching the pre-Phase-35 signature
// shape that analyzed cleanly.
struct ZipImportTarget {
    ZipDest           dest;
    ZipConflictPolicy policy;
};

using WorkerThread = std::jthread;

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

    // Launch a ZIP import (mirrors ui::import_zip). A ZIP with mixed folders under
    // AskUser comes back needs_resolution having written nothing (planning only) —
    // the caller shows a Flatten/Skip modal and re-starts with the chosen policy.
    bool start_zip(vault::Vault& v, std::filesystem::path zip, ZipDest dest,
                   std::string base_gallery, std::string new_gallery_name,
                   ZipConflictPolicy policy);

    // Launch a 7z/RAR/TAR import (mirrors ui::import_archive; Phase 34). Same
    // needs_resolution/mixed-folder contract as start_zip. launch() doesn't care
    // which backend the work closure uses, so this reuses the exact same job
    // machinery as start_zip/start_cbz rather than a parallel job type.
    // `password_protected`/`password` (Phase 35): true/non-empty only for an
    // encrypted zip/cbz the caller already detected via ui::zip_is_encrypted.
    // `password` is moved into the worker and wiped immediately after the
    // import_archive/import_archive_cbz call returns.
    bool start_archive(vault::Vault& v, std::filesystem::path archive, ZipImportTarget target,
                       std::string base_gallery, std::string new_gallery_name,
                       bool password_protected = false, crypto::SecureBytes password = {});

    // Launch a CBR/CB7/CBT import (mirrors ui::import_archive_cbz; Phase 34).
    bool start_archive_cbz(vault::Vault& v, std::filesystem::path archive,
                           std::string base_gallery, std::string gallery_name,
                           bool password_protected = false, crypto::SecureBytes password = {});

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
    // Reset counters and spawn `work` (which returns the outcome) on the worker
    // thread. Shared by start_cbz/start_zip. Returns false if a job is running.
    bool launch(std::function<ZipImportOutcome()> work);

    ImportProgress    progress_;
    std::atomic<bool> active_{false};   // job in flight or finished-but-uncollected
    std::atomic<bool> done_{false};     // worker set this at the end
    ZipImportOutcome  outcome_;         // written by worker; read after join
    WorkerThread      thread_;
};

}  // namespace ui
