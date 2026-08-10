#include "ui/collection_ops.h"

#include <format>
#include <utility>

#include "gfx/window.h"
#include "platform/folder_dialog.h"
#include "platform/path_utf8.h"
#include "ui/import_queue.h"
#include "ui/progress_modal.h"

namespace ui {

CollectionBatchOps::CollectionBatchOps(const Deps& d)
    : vault_(d.vault), win_(d.win), folder_(d.folder_dialog), queue_(d.queue),
      transfer_(d.vault, d.active_path, d.registry, d.file_dialog, d.win, d.second)
{
}

CollectionBatchOps::~CollectionBatchOps()
{
    release_exclusive();
}

void CollectionBatchOps::release_exclusive()
{
    if (had_exclusive_) {
        queue_.set_exclusive(false);
        had_exclusive_ = false;
    }
}

bool CollectionBatchOps::handle_event(const SDL_Event& e)
{
    if (consent_.active()) {
        if (e.type == SDL_EVENT_KEY_DOWN &&
            consent_.handle_key(e.key.key) == ConsentDialog::Result::Confirmed) {
            folder_.open(win_.sdl_window(), platform::FolderDialog::Purpose::Export, false);
        }
        return true;
    }
    if (transfer_.active()) {
        (void)transfer_.handle_event(e);
        return true;
    }
    if (job_.active()) {
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) { job_.cancel(); }
        return true;
    }
    return false;
}

CollectionBatchOps::Poll CollectionBatchOps::poll()
{
    Poll out;

    transfer_.update();
    if (TransferCompletion tc; transfer_.consume_completed(tc)) {
        out.status = std::move(tc.status);
        out.reload = true;   // a Move removed items from the collection
        out.dirty  = true;
        release_exclusive();
    }
    if (!transfer_.active()) { release_exclusive(); }

    if (auto dest = folder_.take_result(platform::FolderDialog::Purpose::Export)) {
        if (!dest->empty() && collect_) {
            // Resolve the selection NOW — never across the async folder pick.
            if (auto nodes = collect_(); !nodes.empty()) {
                const std::string& d = (*dest)[0];
                job_.start_export(vault_, std::move(nodes), platform::utf8_to_path(d), d);
            } else {
                out.status = "Nothing to export.";
            }
        }
        collect_ = nullptr;
        out.dirty = true;
    }

    if (auto oc = job_.take_outcome()) {
        out.status = oc->ok ? std::move(oc->status) : std::move(oc->error);
        out.dirty  = true;
    }
    return out;
}

void CollectionBatchOps::request_export(
    std::size_t count, std::function<std::vector<const vault::IndexNode*>()> collect,
    std::string& status)
{
    if (folder_.busy() || modal_active() || busy()) { return; }
    if (count == 0) {
        status = "Select items first (Space), then [X] to export.";
        return;
    }
    status.clear();
    collect_ = std::move(collect);
    consent_.open(std::format("Export {} {}", count, count == 1 ? "item?" : "items?"));
}

void CollectionBatchOps::request_transfer(std::vector<ParentGroup> media_groups,
                                          std::vector<std::string> gallery_paths,
                                          std::string& status)
{
    if (transfer_.active() || busy()) { return; }
    if (queue_.busy()) {
        status = "Imports running — press Shift+I for status";
        return;
    }
    if (media_groups.empty() && gallery_paths.empty()) { return; }
    status.clear();
    // Phase 50: transfers are exclusive with the import queue.
    queue_.set_exclusive(true);
    had_exclusive_ = true;
    transfer_.open_collection(std::move(media_groups), std::move(gallery_paths));
}

void CollectionBatchOps::render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H)
{
    consent_.render(r, font, W, H);
    transfer_.render(r, font, W, H);
    if (job_.active()) {
        const int done  = job_.done();
        const int total = job_.total();
        const std::string count = std::format("{} / {} files", done, total);
        draw_op_progress(r, font, W, H,
                         {.title = "Exporting…", .count_line = count,
                          .done = done, .total = total});
    }
}

} // namespace ui
