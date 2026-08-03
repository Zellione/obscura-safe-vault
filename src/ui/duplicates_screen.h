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

// Forward declarations for friend functions
void handle_review_key(class DuplicatesScreen& screen, const SDL_KeyboardEvent& key);
void draw_member_tile(gfx::Renderer& r, gfx::FontAtlas& font, const class DuplicatesScreen& screen,
                      const DupMember& member, bool focused, const SDL_FRect& tile_rect);
void draw_group_row(gfx::Renderer& r, gfx::FontAtlas& font, const class DuplicatesScreen& screen,
                    size_t group_idx, float y);

class DuplicatesScreen final : public Screen {
    friend void handle_review_key(DuplicatesScreen& screen, const SDL_KeyboardEvent& key);
    friend void draw_member_tile(gfx::Renderer& r, gfx::FontAtlas& font, const DuplicatesScreen& screen,
                                 const DupMember& member, bool focused, const SDL_FRect& tile_rect);
    friend void draw_group_row(gfx::Renderer& r, gfx::FontAtlas& font, const DuplicatesScreen& screen,
                               size_t group_idx, float y);

public:
    enum class State : uint8_t { Choose, Scanning, Review, Done };

    DuplicatesScreen(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                     gfx::TextureCache& cache, Nav back);

    void handle_event(const SDL_Event& e) override;
    void update(double dt) override;
    void render(gfx::Renderer& r) override;
    [[nodiscard]] bool animating() const override { return state_ == State::Scanning; }
    [[nodiscard]] bool blocks_idle_lock() const override
    {
        return state_ == State::Scanning ||
               (state_ == State::Review && review_.any_marked());
    }
    [[nodiscard]] std::vector<HelpGroup> help_groups() const override;
    void on_vault_changed() override;

private:
    void handle_key(const SDL_KeyboardEvent& key);
    void start_scan(bool perceptual);
    void leave();                       // Esc: back nav (confirm if marks pending)

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
    bool   confirm_apply_ = false;      // Ctrl+Enter pressed, awaiting Y/Enter
    bool   confirm_leave_ = false;      // Esc pressed with pending marks
    std::string status_;                // one-line footer notice (apply refusals etc.)
    std::string done_summary_;

    // Tile pipeline (favorites_images.h pattern).
    image::DecodeWorker          worker_{image::decode_wake_event()};
    std::unordered_set<uint64_t> failed_;
};

} // namespace ui
