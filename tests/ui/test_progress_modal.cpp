#include "test_framework.h"

#include <SDL3/SDL.h>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "ui/progress_modal.h"

namespace {
constexpr const char* kFontPath = OSV_DEFAULT_FONT;

// Headless software renderer (mirrors the gfx draw smoke tests).
struct SoftRenderer {
    SDL_Surface*  surf = nullptr;
    SDL_Renderer* r    = nullptr;
    SoftRenderer()
    {
        surf = SDL_CreateSurface(640, 480, SDL_PIXELFORMAT_RGBA32);
        if (surf) r = SDL_CreateSoftwareRenderer(surf);
    }
    ~SoftRenderer()
    {
        if (r)    SDL_DestroyRenderer(r);
        if (surf) SDL_DestroySurface(surf);
    }
};
} // namespace

// Smoke-test draw_op_progress against a headless renderer: it must not crash for
// the "preparing" (total == 0, no bar fill) and the mid-progress (total > 0, bar
// filled) states — exercising both bar-fill branches.
TEST(progress_modal_draws_without_crashing)
{
    SoftRenderer sr;
    REQUIRE(sr.r != nullptr);
    gfx::Renderer r(sr.r);
    gfx::FontAtlas font;
    REQUIRE(font.bake_from_file(kFontPath, 18.0f));

    // total == 0 → "preparing", no fill drawn.
    ui::draw_op_progress(r, font, 640, 480,
                     {.title = "Exporting…", .count_line = "Preparing…", .done = 0, .total = 0});

    // total > 0, done > 0 → bar fill branch taken.
    ui::draw_op_progress(r, font, 640, 480,
                     {.title = "Moving…", .count_line = "3 / 10 files",
                      .hint = "Esc to cancel", .done = 3, .total = 10});

    SDL_RenderPresent(sr.r);
    CHECK(true);   // reached here without a crash
}
