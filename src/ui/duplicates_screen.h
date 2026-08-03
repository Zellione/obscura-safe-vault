#pragma once

// Duplicate-finder screen: choose scan mode -> background scan with progress ->
// side-by-side group review (mark keep/remove) -> one-commit batch delete.
// Reached from the gallery grid with Ctrl+D.

#include <SDL3/SDL.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "image/decode_worker.h"
#include "ui/dup_model.h"
#include "ui/dup_scan.h"
#include "ui/screen.h"

namespace gfx {
    class Window;
    class FontAtlas;
    class Renderer;
    class TextureCache;
}
namespace vault { class Vault; }

namespace ui {

// Forward declarations for friend functions. The tile/row/inspect painters take
// a non-const screen: drawing a missing thumbnail submits a decode fetch
// (favorites_images pattern — the pipeline members are ordinary, not mutable).
void handle_review_key(class DuplicatesScreen& screen, const SDL_KeyboardEvent& key);
bool consume_overlay_key(class DuplicatesScreen& screen, const SDL_KeyboardEvent& key);
void draw_member_tile(gfx::Renderer& r, gfx::FontAtlas& font, class DuplicatesScreen& screen,
                      const DupMember& member, bool focused, const SDL_FRect& tile_rect);
void draw_group_row(gfx::Renderer& r, gfx::FontAtlas& font, class DuplicatesScreen& screen,
                    size_t group_idx, float y, const struct DupRowLayout& lay);
void draw_confirm_apply_overlay(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H, const class DuplicatesScreen& screen);
void draw_confirm_leave_overlay(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H, const class DuplicatesScreen& screen);
void draw_inspect_overlay(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H, class DuplicatesScreen& screen);

class DuplicatesScreen final : public Screen {
    friend void handle_review_key(DuplicatesScreen& screen, const SDL_KeyboardEvent& key);
    friend bool consume_overlay_key(DuplicatesScreen& screen, const SDL_KeyboardEvent& key);
    friend void draw_member_tile(gfx::Renderer& r, gfx::FontAtlas& font, DuplicatesScreen& screen,
                                 const DupMember& member, bool focused, const SDL_FRect& tile_rect);
    friend void draw_group_row(gfx::Renderer& r, gfx::FontAtlas& font, DuplicatesScreen& screen,
                               size_t group_idx, float y, const DupRowLayout& lay);
    friend void draw_confirm_apply_overlay(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H, const DuplicatesScreen& screen);
    friend void draw_confirm_leave_overlay(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H, const DuplicatesScreen& screen);
    friend void draw_inspect_overlay(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H, DuplicatesScreen& screen);

public:
    enum class State : uint8_t { Choose, Scanning, Review, Done };

    DuplicatesScreen(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                     gfx::TextureCache& cache, Nav back);
    ~DuplicatesScreen() override;

    void handle_event(const SDL_Event& e) override;
    void update(double dt) override;
    void render(gfx::Renderer& r) override;
    [[nodiscard]] bool animating() const override { return state_ == State::Scanning; }
    [[nodiscard]] bool blocks_idle_lock() const override
    {
        // Review blocks the idle lock only once the user has actually touched
        // the marks — the pre-applied defaults alone are not invested work.
        return state_ == State::Scanning ||
               (state_ == State::Review && review_.touched() && review_.any_marked());
    }
    [[nodiscard]] std::vector<HelpGroup> help_groups() const override;
    void on_vault_changed() override;

private:
    // Pending-confirmation overlays (each owns every key while up).
    struct ConfirmState {
        bool apply = false;   // Ctrl+Enter pressed, awaiting Y/Enter
        bool leave = false;   // Esc pressed with pending marks
    };
    // Full-screen inspect of the focused member's decoded original. The
    // texture is OWNED here, never stored in the shared thumbnail cache: a
    // full-resolution upload there evicts the review thumbnails and can
    // itself be evicted mid-inspect (image blanks to the backdrop). Mirrors
    // the FullTexCache rationale for the viewer.
    struct InspectState {
        std::optional<uint64_t> key;   // inspect request key (bit 63 set)
        bool decoding = false;         // waiting for worker to decode
        std::optional<image::ImageData> image;  // decoded, awaiting GPU upload
        SDL_Texture* tex = nullptr;    // owned; destroyed by close_inspect()
    };

    void handle_key(const SDL_KeyboardEvent& key);
    void start_scan(bool perceptual);
    void leave();                       // Esc: back nav (confirm if marks pending)

    // handle_review_key / update / render helpers (complexity kept per-piece).
    void apply_marked_batch();          // accepted confirm: one-commit delete
    void follow_focus(int delta);       // Up/Down group focus + scroll-follow
    void request_inspect();             // Enter: decode the focused original
    void close_inspect();               // destroy the owned texture, clear state
    void pump_decode_results();         // drain worker results (tiles + inspect)
    void take_inspect_result(image::DecodeWorker::Result& res);
    void finish_scan(DupScanOutcome outcome);
    void render_choose(gfx::Renderer& r, float W, float H);
    void render_scanning(gfx::Renderer& r, float W, float H);
    void render_review(gfx::Renderer& r, float W, float H);

    gfx::Window&       win_;
    gfx::FontAtlas&    font_;
    vault::Vault&      vault_;
    gfx::TextureCache& cache_;
    Nav                back_;

    State      state_       = State::Choose;
    int        choose_sel_  = 0;        // 0 = Exact, 1 = Exact + similar
    DupScanJob job_;
    DupReview  review_;
    size_t     skipped_     = 0;
    bool       stale_       = false;    // vault changed under the review

    // Review navigation / apply state (Tasks 7-8).
    size_t focus_group_  = 0;
    size_t focus_member_ = 0;
    float  scroll_       = 0.0f;
    ConfirmState confirm_;
    InspectState inspect_;
    std::string status_;                // one-line footer notice (apply refusals etc.)
    std::string done_summary_;

    // Tile pipeline (favorites_images.h pattern).
    image::DecodeWorker          worker_{image::decode_wake_event()};
    std::unordered_set<uint64_t> failed_;
};

} // namespace ui
