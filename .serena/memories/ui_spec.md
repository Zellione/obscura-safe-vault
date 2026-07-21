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

## Detail panel (Phase 48)
Toggleable right-edge panel on the gallery grid, the favorites/tag screens, and
advanced search. `D` toggles it (`Ctrl+D` on advanced search, where bare letters
go to the query buffer); `Ctrl+Up`/`Ctrl+Down` scroll it (keyboard only — the
mouse wheel still scrolls the grid, not the panel). Opening
it reflows the grid into the reduced width rather than overlaying tiles; below a
640 px window it stays hidden. Shows the focused node's name, type/codec,
dimensions, size, date, own tags and the inherited cascade; a gallery shows a
recursive tally + total size; a multi-selection shows an aggregate summary.
Open/closed state is session-global via `GallerySessionState::detail_open`.
