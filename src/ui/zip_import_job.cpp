#include "ui/zip_import_job.h"

#include <utility>

namespace ui {

ZipImportJob::~ZipImportJob()
{
    progress_.cancel.store(true);   // stop the loop promptly on teardown
    if (thread_.joinable()) thread_.join();
}

bool ZipImportJob::launch(std::function<ZipImportOutcome()> work)
{
    if (active_.load()) return false;   // one import at a time

    progress_.total.store(0);
    progress_.done.store(0);
    progress_.cancel.store(false);
    outcome_ = {};
    done_.store(false);
    active_.store(true);

    thread_ = WorkerThread([this, work = std::move(work)]() mutable {
        outcome_ = work();
        done_.store(true);   // release: happens-before the joining thread's read
    });
    return true;
}

bool ZipImportJob::start_cbz(vault::Vault& v, std::filesystem::path cbz,
                             std::string base_gallery, std::string gallery_name)
{
    // `v` is a reference the caller keeps alive for the job's lifetime; the path
    // and names are moved into the work closure so it owns them independently.
    return launch([this, &v, cbz = std::move(cbz), base = std::move(base_gallery),
                   name = std::move(gallery_name)]() {
        return import_cbz(v, cbz, base, name, &progress_);
    });
}

bool ZipImportJob::start_zip(vault::Vault& v, std::filesystem::path zip, ZipDest dest,
                             std::string base_gallery, std::string new_gallery_name,
                             ZipConflictPolicy policy)
{
    return launch([this, &v, zip = std::move(zip), dest, base = std::move(base_gallery),
                   name = std::move(new_gallery_name), policy]() {
        return import_zip(v, zip, dest, base, name, policy, &progress_);
    });
}

std::optional<ZipImportOutcome> ZipImportJob::take_outcome()
{
    if (!active_.load() || !done_.load()) return std::nullopt;
    if (thread_.joinable()) thread_.join();   // synchronises outcome_ into this thread
    active_.store(false);
    return outcome_;
}

}  // namespace ui
