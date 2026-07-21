# UI / UX specification

Original screen design intent (Phase 5–6, 39). Feature-by-feature evolution
since (List view, video playback, slideshow, tags, advanced search, etc.) is
tracked in `mem:core`'s `ui/` section, not here — this memory stays the
foundational spec.

## Unlock screen (Phase 5)
- Password field (text is masked).
- Optional keyfile picker button (`SDL_ShowOpenFileDialog`).
- Create New Vault: passphrase-strength meter; offer to generate a random
  passphrase (password is the genuine security boundary).

## Gallery grid (Phase 5)
- Tile grid: sub-galleries (folder icon) and image thumbnails, rendered
  folders-first (Phase 46).
- Breadcrumb bar at top shows current path.
- Keyboard: `Enter`/`Space` open, `Backspace`/`Esc` up.
- Import via SDL file dialog; thumbnails generated + stored on import.

## Image viewer (Phase 6)
- Top ~75%: big image, fit-to-window by default. Mouse wheel / `+`/`-` zoom;
  drag or arrow keys (when zoomed) pan.
- Bottom ~25%: horizontal thumbnail strip, scrolled to + highlighting the
  current image. `Left`/`Right` prev/next in the leaf gallery; `Up`/`Esc`
  back to gallery grid.

## Help popup convention (Phase 39)
Single global `F1` popup, context-sensitive shortcuts grouped by task/area —
replaces the prior inline-footer-string approach (which ran off-window at
normal sizes). `Screen::help_groups()` virtual supplies per-screen content;
`App` owns the one `HelpPopupState` and renders the overlay on top of
whichever screen is active. Close with `Esc` or `Q`.

## Fixed chrome bands — reserve, never overlay

Screens with a fixed header and/or footer reserve that space as an **opaque**
band and lay content out strictly between the bands. Two rules, both learned
from the versions that broke them:

1. **No alpha-keyed chrome.** A translucent band over scrolling or zooming
   content leaves its own text washed out by whatever passes behind it. Bands
   are drawn at full alpha (`BG` on the gallery grid, `STRIP_BG` in the viewer)
   with a `BORDER` hairline on the content-facing edge.
2. **A band never covers content.** The scrollable/zoomable area stops at the
   band, rather than continuing underneath it. On the gallery grid that means
   culling, clipping, scroll-clamping and hit-testing all key off
   `content_bottom()`, not the window height. In the viewer it means the image
   or video is fit into the band-inset rect, so no part of the picture is hidden.

Geometry comes from the pure `ui::split_chrome` (`chrome_layout.*`); drawing
from `ui::draw_chrome_band` (`widgets.*`).

**Viewer specifics.** Windowed, header and footer are always reserved, so the
image never resizes when a status message comes or goes. Fullscreen is the
deliberate exception: it already hides the thumbnail strip, and it drops both
bands (HUD text included) for an edge-to-edge picture — a footer message still
forces its band back in so it stays legible.

## Detail panel (Phase 48)
Toggleable right-edge panel on the gallery grid, the favorites/tag screens, and
advanced search. `D` toggles it (`Ctrl+D` on advanced search, where bare letters
go to the query buffer); `Ctrl+Up`/`Ctrl+Down` scroll it, as does the mouse wheel
while the cursor is over the panel strip (`detail_panel_hit` derives that region
from `detail_panel_width`, so it can never disagree with the reserved width; the
host consumes the event so the grid does not also scroll). Opening
it reflows the grid into the reduced width rather than overlaying tiles; below a
640 px window it stays hidden. Shows the focused node's name, type/codec,
dimensions, size, date, own tags and the inherited cascade; a gallery shows a
recursive tally + total size; a multi-selection shows an aggregate summary.
Open/closed state is session-global via `GallerySessionState::detail_open`.
