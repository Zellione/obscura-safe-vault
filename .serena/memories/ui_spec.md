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

**Phase 51 redesign:** The popup now renders at full window height (clamped by content
or content_max), reflows into two columns above a width threshold, and scrolls by
**line index** (not pixels) so content always aligns to LINE_H boundaries. Clip band
sized to exactly the visible lines, computed via `help_visible_lines(popup_h, lines_per_column)`;
scroll clamped to `max(0, total_lines - visible_lines)`. A scroll affordance (theme TEXT_FAINT)
appears when content overflows. No partial lines clip at viewport edges at any window size.

Phase 49 base: `draw_help_popup` prepends a synthesised "Global" group listing `F1` Help
and `F2` Settings, since `help_groups()` is per-screen and had no shared entry point.

## Tag chips (Phase 49)

Stored tags render as **chips** — a small filled dot in the category's colour
plus the bare name — on every surface that shows tags: the detail panel, the tag
editor (own tags, the read-only inherited section, and the autosuggest
dropdown), the tag-overview rows, grid tiles and List rows. A tag whose prefix
matches a **configured** category shows the suffix only; anything else renders
verbatim in `TEXT_DIM`, so `12:30` and `Re:Zero` are never mangled. Storage,
search, autosuggest and matching all still operate on the full
`"artist:Kaguya"` string — this is a display transform only.

Two consequences the design accepts: `female:glasses` and `male:glasses` both
read "glasses", separated only by dot colour; and the tag-galleries /
tag-images screen **headers stay plain text** (they are composed `title_`
strings rendered by the shared `FavoritesScreen` base, and a header is not a
scannable list).

On grid tiles the chip line is reserved **per gallery**, never per tile — if no
child in the listing carries a tag, no space is reserved at all, so an untagged
vault looks exactly as it did before and rows never go ragged. The cell does not
grow: the label moves up by `CHIP_ROW_H` and the thumbnail shrinks by the same
amount, leaving every grid metric (and therefore hit-testing) untouched.
`tiles_show_tags` turns the line off entirely.

## Tag overview screen (Phase 22, Phase 51 redesign)

Scrollable list of distinct tags across the vault. Each row is now **two lines**:

1. **Tag + counts** — chip (dot + name) on the left, gallery count / image count on the right,
   dimmed. E.g., `● artist` on the left, `3 galleries · 5 images` on the right.
2. **Description** — dim text, either the per-tag description or `(no description — [E] to add)`.

**[E] inline edit:** pressing `[E]` on any row opens a prompt, mirroring the settings-overlay
pattern — take the entered text, save to vault via `vault::set_tag_description`, refresh the
list. Empty input removes an existing description. Failed saves surface on the error line, never
as success. Description drawn via `ui::fit_text` (truncated to content width, elided).

**Deliberately unchanged:** `VaultSearch::tag_overview()` counts are direct-tag only (Phase 22
chose this so the cascade could not inflate counts). The two-line row layout reserves exactly
one row per listing, never per tag, mirroring the Phase 49 chip-row reservation scheme so no
grid metric changes.

Navigation: Up/Down move rows, Enter opens TagGalleries for that tag, Tab toggles sort (Name/Count),
type-ahead filters by name prefix, `` ` `` quick-switch vault.

## Sub-gallery tile counts (Phase 51)

Gallery tiles now show a small **counts row** beneath the label (dim text, `CHIP_ROW_H`
reserve per listing, never per tile): "3 galleries · 12 items" / "1 gallery · 1 item" /
"12 items" / "empty". Items = images + videos. The count is **direct children only** and
deliberately disagrees with the `[D]` detail panel's recursive total tally — label wording
("directly inside", "children only") makes the scope unambiguous.

The cell does not grow: thumbnail shrinks by `CHIP_ROW_H`, label moves up, every grid metric
(cols, cell size, hit-testing) untouched. Same reservation scheme as Phase 49 tags: space
reserved per gallery listing only (no sub-galleries → nothing reserved).

## Settings overlay (Phase 49)

Global `F2`, mirroring the `F1` convention: `App` intercepts the key, owns one
`SettingsState`, and draws the overlay over whichever screen is active. It is
deliberately **not** a `Screen` — a screen would force `App` to reconstruct
what the user came from on `Esc`, which loses a paused video or a scroll
position.

Sidebar rail + content pane. `Tab` moves between rail and pane, `↑↓` row, `←→`
change value, `N` add category, `R` rename, `Del` remove, `Esc` close. Three
sections:
- **Appearance — this machine.** Theme; changes apply live and persist to
  `theme.conf` immediately, exactly as the retired `C` theme picker did. The
  preview IS the choice.
- **Browsing — this vault.** Vault-wide default sort order, and "show tags on
  tiles".
- **Tag colours — this vault.** The category→swatch rows.

With no vault unlocked the two vault-scoped sections render a single dim
"Unlock a vault to configure" line, and value keys are not routed there. A
failed save surfaces in the overlay's own error line — it is never reported as
success. `C` on the vault manager opens this overlay focused on Appearance
(`NavKind::ToSettings`); the standalone theme picker is gone.

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

**Phase 51:** A gallery's detail panel now includes a read-only **"From contents"**
tag section (below "Inherited from gallery") showing the tags carried by its descendants.
Rendered as chips via the existing `DetailSection::is_tags` path; marked non-editable so
Del/selection never touch it.

## Import Status screen (Phase 50)
Global `Shift+I` opens `NavKind::ToImportStatus`, or click the footer import summary.
Shows:
- **Running item:** name, progress bar (done/total chunks), source kind (Files/Zip/Archive/Folder).
- **Queued items:** list, `Ctrl+Up`/`Ctrl+Down` reorder, `Del` cancel per-item.
- **Finished/failed items:** outcomes (imported/skipped counts, error text). `C` clears finished entries.
- **Lane-failure banner:** surfaces hard-stop commit errors (hard stop, queue halted).
- **Help group (F1):** standard. **Esc** returns to previous screen.

### Footer bar (Phase 50)
Live summary while queue is non-empty: `"Importing <name> 128/450 · 2 queued"` (done/total, remainder queued).
**Priority:** error > import summary > status. Clickable to jump to ImportStatusScreen.

### Lock confirm modal (Phase 50)
Manual lock, vault switch, or quit with pending queue: default-cancel modal reads **"N imports pending — finish current file, discard the rest, and lock?"**
On confirm (Y): current file completes, queue discarded, final commit-lane flush, passwords/keys wiped.
SDL_EVENT_QUIT also flows through this gate.
