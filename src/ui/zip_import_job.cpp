#include "ui/zip_import_job.h"

#include <utility>

namespace ui {

ZipImportJob::~ZipImportJob()
{
    progress_.cancel.store(true);   // stop the loop promptly on teardown
    if (thread_.joinable()) thread_.join();
}

bool ZipImportJob::start_cbz(vault::Vault& v, std::filesystem::path cbz,
                             std::string base_gallery, std::string gallery_name)
{
    if (active_.load()) return false;   // one import at a time

    progress_.total.store(0);
    progress_.done.store(0);
    progress_.cancel.store(false);
    outcome_ = {};
    done_.store(false);
    active_.store(true);

    // `v` is a reference the caller keeps alive for the job's lifetime; the path
    // and names are moved into the worker so it owns them independently.
    thread_ = std::thread(
        [this, &v, cbz = std::move(cbz), base = std::move(base_gallery),
         name = std::move(gallery_name)]() mutable {
            outcome_ = import_cbz(v, cbz, base, name, &progress_);
            done_.store(true);   // release: happens-before the joining thread's read
        });
    return true;
}

std::optional<ZipImportOutcome> ZipImportJob::take_outcome()
{
    if (!active_.load() || !done_.load()) return std::nullopt;
    if (thread_.joinable()) thread_.join();   // synchronises outcome_ into this thread
    active_.store(false);
    return outcome_;
}

}  // namespace ui
