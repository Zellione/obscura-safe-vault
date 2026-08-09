#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <string>
#include <vector>

#include "ui/consent_dialog.h"
#include "ui/file_op_job.h"
#include "ui/parent_group.h"
#include "ui/transfer_dialog.h"

namespace gfx { class Renderer; class FontAtlas; class Window; }
namespace platform { class FileDialog; class FolderDialog; class VaultRegistry; }
namespace vault { class Vault; struct IndexNode; }

namespace ui {

class ImportQueue;
class SecondVaultSession;

// The Phase 68 batch operations a collection screen (favorites, tags, advanced
// search results) runs over its Space/Ctrl+A selection: consent-gated export
// (X) and per-parent grouped move/copy (M). One shared component so the modal
// flows, the Phase 50 import-queue exclusivity, and the progress rendering
// cannot drift between screens. The gallery grid keeps its own richer flows.
//
// Contract: while busy() the worker thread owns the vault handle(s) — the host
// screen must not walk the tree or submit thumbnail decodes (its render should
// draw chrome + render() only), exactly like GalleryGrid's vault_busy.
class CollectionBatchOps {
public:
    struct Deps {
        vault::Vault&           vault;
        std::string             active_path;    // active vault's file path
        platform::VaultRegistry& registry;
        platform::FileDialog&   file_dialog;    // destination-vault keyfile pick
        gfx::Window&            win;
        SecondVaultSession*     second;         // Phase 66 warm slot (may be null)
        platform::FolderDialog& folder_dialog;  // App-owned (callback outlives screens)
        ImportQueue&            queue;          // Phase 50 exclusivity gate
    };

    explicit CollectionBatchOps(const Deps& d);
    ~CollectionBatchOps();

    CollectionBatchOps(const CollectionBatchOps&)            = delete;
    CollectionBatchOps& operator=(const CollectionBatchOps&) = delete;

    // A worker owns the vault handle(s) — host must not touch the vault.
    [[nodiscard]] bool busy() const noexcept
    { return job_.active() || transfer_.job_active(); }

    // Any modal of ours (consent / transfer dialog / running job) captures input.
    [[nodiscard]] bool modal_active() const noexcept
    { return consent_.active() || transfer_.active() || job_.active(); }

    // Modal-priority input. Returns true when the event was consumed.
    bool handle_event(const SDL_Event& e);

    // Frame poll: drains the folder pick, the job outcome, and the transfer
    // completion. `status` is set when something finished; `reload` when the
    // host's collection content changed (a Move removed items).
    struct Poll {
        std::string status;
        bool        reload = false;
        bool        dirty  = false;
    };
    [[nodiscard]] Poll poll();

    // X: consent modal → folder pick → export job. `collect` runs when the
    // folder pick lands and must return the CURRENT selection's nodes (never
    // cached pointers — a background import drain may have reloaded the host).
    void request_export(std::size_t count,
                        std::function<std::vector<const vault::IndexNode*>()> collect,
                        std::string& status);

    // M: one transfer-dialog run over per-parent media groups + gallery
    // subtrees. Refuses (status message) while imports are running (Phase 50).
    void request_transfer(std::vector<ParentGroup> media_groups,
                          std::vector<std::string> gallery_paths, std::string& status);

    // Draw the consent modal, the transfer dialog, and the job progress modal.
    void render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H);

private:
    void release_exclusive();

    vault::Vault&           vault_;
    gfx::Window&            win_;
    platform::FolderDialog& folder_;
    ImportQueue&            queue_;

    ConsentDialog  consent_;
    FileOpJob      job_;
    TransferDialog transfer_;
    std::function<std::vector<const vault::IndexNode*>()> collect_;
    bool           had_exclusive_ = false;
};

} // namespace ui
