## Phase 31 — Fullscreen viewing + edge-click navigation ✅

**Goal:** Let the image viewer (and its in-viewer video playback) expand to a
borderless fullscreen window, with normal navigation unaffected, plus
left/right-edge click navigation between images.

### Tasks
- [x] `src/gfx/window.{h,cpp}` — `Window::set_fullscreen(bool)` /
  `is_fullscreen()`: borderless-maximized toggle that saves/restores the
  windowed position and size (`SDL_GetDisplayUsableBounds` of the window's
  current display; a lookup failure logs and leaves the window unchanged).
- [x] `src/ui/viewer_model.h` — pure `edge_nav_hit(x, vp_x, vp_w) -> int`
  (`EDGE_NAV_FRAC = 0.12f`), unit-tested alongside the existing zoom/pan/strip
  helpers in this file.
- [x] `src/ui/image_viewer.cpp` wiring:
  - `F11` and a double left-click both toggle fullscreen, for images and
    in-viewer video alike (the double-click check runs after the thumbnail-
    strip hit test, so double-clicking a thumbnail still just selects it).
  - `Esc` exits fullscreen on the first press (stays in the viewer); a second
    `Esc` then returns to the gallery as before.
  - Clicking the left/right 12% edge of a non-zoomed image in Fit mode steps
    to the previous/next image (images only — video keeps its seek-bar/
    play-pause click targets; FillScroll already has its own scroll-based
    navigation).

**Out of scope (YAGNI):** the standalone slideshow view (already presents
full-screen-ish by design); true exclusive `SDL_SetWindowFullscreen`
display-mode switching; edge-click navigation for video or FillScroll mode.

### Acceptance criterion
From the image viewer, `F11` or a double-click expands the window to a
borderless fullscreen covering the display and back again, for both images
and in-viewer video; `Esc` exits fullscreen on the first press and returns to
the gallery on the second; arrow-key/thumbnail-strip navigation and video
controls are unaffected. Clicking the left/right 12% edge of a non-zoomed
image in Fit mode steps to the previous/next image, windowed or fullscreen.

**Status:** ✅ All tests pass (`scripts/test.sh`); `scripts/test.sh --asan`
clean. `gfx::Window`'s fullscreen SDL calls are thin wrappers verified by
manual smoke test (not unit-tested — matches the existing
`test_window_visibility.cpp` precedent of only testing pure helpers under
headless CI).
