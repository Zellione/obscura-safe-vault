## Phase 6 — Image viewer ✅

**Goal:** Full-screen image viewing with zoom/pan and the auto-scrolling thumbnail strip.

### Tasks
- [x] `src/ui/image_viewer.{h,cpp}`:
  - [x] Top ~75%: big image rendered via `gfx::Renderer::draw_image` with zoom + pan.
    - [x] Fit-to-window on first display.
    - [x] Mouse wheel / `+` / `-`: zoom in/out centred on cursor.
    - [x] Drag (LMB held) or arrow keys (when zoomed): pan.
  - [x] Bottom ~25%: horizontal thumbnail strip.
    - [x] Scrolls to centre the current image's thumbnail.
    - [x] Current thumbnail highlighted (border / tint).
    - [x] Click or `Left`/`Right` arrow: change current image; strip auto-scrolls.
  - [x] `Up` / `Esc`: back to gallery grid.
- [x] App state machine: add `Viewing` state.
- [x] `tests/ui/`:
  - [x] Zoom clamped to sane min/max (e.g., 5%–2000%).
  - [x] Pan clamped so image cannot be dragged entirely off-screen.
  - [x] `Left`/`Right` wrap correctly at gallery boundaries (first/last image).
  - [x] Thumbnail-strip scroll position is correct for galleries of various sizes.

### Acceptance criterion
Open a vault, navigate to a leaf gallery, click an image: viewer opens. Arrow keys navigate; zoom/pan work. Thumbnail strip scrolls and highlights correctly.

**Status:** ✅ 116/116 tests pass under `scripts/test.sh` and ASAN+UBSan+LSan; the app
builds and links. All zoom/pan/strip math is factored into a pure, SDL-free,
headlessly-tested unit (`src/ui/viewer_model.h`), mirroring the `nav_model` /
`unlock_logic` / `widgets` pattern; `ImageViewer` owns only the SDL plumbing.

> **Notes / decisions made during implementation**
> - **Pan clamp.** Rather than the looser "not entirely off-screen", the clamp keeps the
>   image *contained*: when scaled larger than the viewport it always covers it (no
>   background gap); when smaller it stays fully inside. Both reduce to the symmetric
>   bound `|pan| <= |scaled - view| / 2`.
> - **Arrow-key overloading.** `Left`/`Right` change image when fit-to-window, but pan
>   when zoomed in (per the ROADMAP's "arrow keys (when zoomed): pan"). `Esc` is always
>   "back"; `Up` is "back" only when not zoomed (it pans up when zoomed). `0` re-fits.
> - **Full-image texture** is owned by the viewer and rebuilt only when the current image
>   changes — it is kept out of the shared `TextureCache` so a single large decode can't
>   evict every gallery thumbnail. Thumbnails still share the cache (keyed by
>   `data_offset`) with the gallery grid. The decrypted original lives only in a transient
>   mlock'd `SecureBytes` during decode (invariant #1).
> - **Nav payload.** `ui::Nav` gained `path` + `index` so transitions carry context:
>   `ToViewer` opens a leaf gallery at an image index, and the viewer's `ToGallery` return
>   restores the grid at the same path with the viewed image selected.
