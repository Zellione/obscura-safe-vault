## Phase 11 — Slideshow ✅

**Goal:** Auto-advancing full-screen viewing of a leaf gallery, with a
user-configurable dwell time and a clear on/off toggle.

### Tasks
- [x] `src/ui/slideshow_model.{h,cpp}` — pure, SDL-free state machine (mirrors `viewer_model`/`scroll_model`): running/paused state, elapsed-time accumulation, advance + wrap (loop at gallery end), optional shuffle (visits each image once per cycle), and cross-fade progress `0..1`. Fully unit-tested headlessly.
- [x] **Configurable dwell** — the per-image display duration is user-adjustable live (`[`/`]` or `+`/`-` to shorten/lengthen), session-scoped (not persisted into the vault), with a sane default (4 s) and clamp range (1–30 s).
- [x] **On/off toggle** — a single key starts/stops the slideshow (`P` play/pause); `Space` (and a mouse click) also pauses; `Esc`/`Up` exits to the viewer. The running state is reflected on-screen (a play/pause + interval indicator).
- [x] **Frame prefetch** — reuse the viewer's existing bounded neighbour-decode window to pre-decode the next image so advances are seamless; decrypted bytes stay in the existing mlock'd `SecureBytes` window (no disk, wiped after GPU upload).
- [x] **Cross-fade** — simple alpha blend between outgoing and incoming frames driven by the model's transition progress.
- [x] `tests/ui/` — advance timing fires at the configured dwell; wrap/loop at the last image; shuffle visits each index exactly once per cycle; pause halts the timer; dwell clamps to the valid range; transition progress clamps to `[0,1]`.

### Acceptance criterion
Starting a slideshow in a leaf gallery auto-advances at the configured dwell
time, cross-fades between images, loops at the end, and the on/off toggle plus
live dwell adjustment work; exiting returns to the viewer at the current image.

**Status:** ✅ 202/202 tests pass under `scripts/test.sh` and `--asan` (9 new
slideshow-model tests). The slideshow is a third `ImageViewer` view mode
(`P` to start; `Fit`/`FillScroll`/`Slideshow`) so exiting (`Esc`/`Up`) returns
to the viewer at the current image. All timing/wrap/shuffle/cross-fade maths live
in the pure, SDL-free `src/ui/slideshow_model.{h,cpp}` (mirroring
`viewer_model`/`scroll_model`); the viewer owns the SDL plumbing — the model is
driven from `update(dt)`, frames decode through the existing bounded
`acquire_full` neighbour window (mlock'd `SecureBytes`, wiped after GPU upload —
no disk), and the cross-fade reuses the renderer's per-draw alpha (textures get
`SDL_BLENDMODE_BLEND`).

> **Notes / decisions made during implementation**
> - **Slideshow = a view mode, not a separate screen.** It reuses the viewer's
>   `acquire_full`/`evict_full_except` bounded texture cache and `update(dt)`
>   loop; entering snapshots nothing new, exiting just flips back to `Fit` at
>   `index()`. Keeps it consistent with the existing `Fit`/`FillScroll` toggle.
> - **Dwell + shuffle persist per-viewer (session-scoped), not in the vault.**
>   `[`/`]` (and `+`/`-`) adjust dwell by 1 s within a 1–30 s clamp; `S` toggles
>   shuffle (re-seeds the model, preserving run state). Both reset when the viewer
>   is re-opened from the gallery, matching the strip-side/view-mode convention.
> - **Cross-fade** draws the outgoing frame opaque then the incoming frame on top
>   at `alpha = fade_progress`, so the blend resolves to `in·p + out·(1−p)` over
>   any background. Fade always plays to completion even if paused mid-transition.
> - **Shuffle** keeps a permutation pinned so the first image shown is the one the
>   viewer was on; it reshuffles only at a true cycle boundary, guaranteeing every
>   image is visited exactly once per cycle.
> - **Structure (post-review).** To keep `ImageViewer` under SonarCloud's
>   field/method limits, the slideshow SDL plumbing was extracted into
>   `src/ui/slideshow_view.*` and the decode→GPU texture cache into a shared
>   `src/ui/full_tex_cache.*` (also now used by Fit/FillScroll). The slideshow's
>   `std::mt19937_64` only picks *display order* — never security material (all
>   crypto randomness stays on the CSPRNG) — so its weak-PRNG hotspots are
>   reviewed as Safe.
