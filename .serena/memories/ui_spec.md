# UI / UX specification

Original screen design intent (Phase 5–6, 39). Feature-by-feature evolution
since (List view, video playback, slideshow, tags, advanced search, etc.) is
tracked in `mem:core`'s `ui/` section, not here — this memory stays the
foundational spec.

## Text fields — editing model (Phase 54)

**Every text field in the app is fully editable and behaves identically.** There
is one model (`ui/text_input_model.h`), one event handler
(`ui::handle_text_input_event`), and one pair of draw helpers
(`ui::draw_edit_field` for boxed fields, `ui::draw_inline_edit_text` for the
ones laid out inline). A screen adding a field wires those three; it does not
invent its own key handling.

- **Keys:** `Left`/`Right` by character, `Ctrl+Left`/`Ctrl+Right` by word,
  `Home`/`End`, any of those with `Shift` to extend the selection, `Ctrl+A`
  select all, `Ctrl+C`/`Ctrl+X` copy/cut, `Ctrl+V` and `Shift+Insert` paste,
  `Backspace`/`Delete` by character or selection. `ui::text_editing_help_group()`
  is the shared `F1` entry; screens hosting fields append it to `help_groups()`.
- **Backspace deletes a CHARACTER, not a byte.** Before Phase 54 every field
  used `std::string::pop_back()`, silently corrupting multi-byte UTF-8.
- **Key precedence: a focused field consumes `Ctrl+A`/`C`/`X`/`V` BEFORE its
  host screen.** Otherwise the gallery's Phase 53 `Ctrl+A` (select all tiles)
  fires while the user is selecting the name they just typed. Hosts call
  `handle_text_input_event()` first and return early when it consumes the event.
  Deliberately NOT consumed, so screen logic keeps working: `Enter`, `Esc`,
  `Tab`, `Up`/`Down`, and `Ctrl+Up`/`Ctrl+Down` (detail-panel scroll).
- **`field_owns_event()` is for hosts whose EMPTY buffer has its own keys** —
  the advanced-search builder (`Left`/`Right` switch tag group, `Del` removes a
  committed tag, `Backspace` drops the last chip), the tag editor, the
  tag-overview filter. With text present the field owns every editing key; with
  the buffer empty only typing and pasting are the field's and the rest falls
  through. Routing unconditionally would silently eat all of it.
- **Password fields accept paste but refuse copy and cut.** `Ctrl+C`/`Ctrl+X`
  are *consumed* (so they never reach a screen shortcut) but do nothing, and
  `SecureTextInput::selection_text()` always returns empty as a second line of
  defence. The Phase 45 copy-password action — which arms a visible auto-clear
  timer — stays the only way plaintext leaves a password field. Paste in adds no
  exposure the OS clipboard does not already have.
- **Recorded limitation:** `gfx::FontAtlas` bakes printable ASCII 32–126 only.
  Non-ASCII is stored, pasted and round-tripped correctly but renders as
  nothing. Caret maths use the same `measure()` the renderer draws with, so
  there is no phantom caret over an invisible glyph. Extending the atlas is out
  of scope.

## Unlock screen (Phase 5)
- Password field (text is masked, one `*` per CHARACTER — Phase 54; per-byte
  masking would put the caret in the wrong place after a multi-byte character).
- Optional keyfile picker button (`SDL_ShowOpenFileDialog`).
- Create New Vault: passphrase-strength meter; offer to generate a random
  passphrase (password is the genuine security boundary).

## Gallery grid (Phase 5)
- Tile grid: sub-galleries (folder icon) and image thumbnails, rendered
  folders-first (Phase 46).
- Breadcrumb bar at top shows current path.
- Keyboard: `Enter`/`Space` open, `Backspace`/`Esc` up.
- **Phase 56:** `Right-click` is Esc — clears any active multi-selection first, then ascends
  to the parent gallery (exactly like Esc). At the root gallery, right-click exits to the vault
  manager.
- `Ctrl+A` (Phase 53) toggles select-all over the current gallery's **direct
  children only** — never recursive, so it matches what the user can see. It is
  handled BEFORE the plain-letter switch, otherwise `A` swallows it. Selectable
  means image, video **or** gallery (`ui::is_selectable`, the one rule shared
  with `Space`; galleries used to be silently unselectable here). Toggle rule:
  all-selected → clear, otherwise select all. On an EMPTY gallery it is inert —
  `select_all(0)` does not clear and `all_selected(0)` is false, so Ctrl+A in an
  empty gallery cannot wipe a selection made elsewhere.
- Import via SDL file dialog; thumbnails generated + stored on import.

## Image viewer (Phase 6)
- Top ~75%: big image, fit-to-window by default. Mouse wheel / `+`/`-` zoom;
  drag or arrow keys (when zoomed) pan.
- Bottom ~25%: horizontal thumbnail strip, scrolled to + highlighting the
  current image. `Left`/`Right` prev/next in the leaf gallery; `Up`/`Esc`
  back to gallery grid.
- **Phase 56:** Viewer re-binds by path when the vault tree changes (background import commits),
  preserving zoom, pan, fill-scroll offset, video position and GIF frame. The current item's
  path is remembered and looked up in the refreshed list; if found, the index is updated and
  nothing else changes; if deleted or moved away, falls back to showing the same index (now a
  different item). **Right-click** is a universal "back / up one level" (via synthetic Esc): in
  borderless fullscreen the first right-click leaves fullscreen, the second returns to gallery.

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
**Phase 56:** The Global group gains **`Right-click — Back / up one level`**.

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
`` ` `` quick-switch vault.

**Filtering (Phase 54 fix):** `/` enters an explicit filter mode; typing then
filters by name prefix, `Esc`/`Backspace` on an empty filter leaves the mode.
The mode flag is load-bearing — the bare letter keys cannot be repurposed for
type-ahead because `E` already opens the description prompt. Before Phase 54 the
filter was documented but **unreachable**: its gate read
`(!filter_.empty() || c == '/') && c != '/'`, which is false for every input.

**[Ctrl+I] tag dictionary import (Phase 55):** opens a `*.json` file dialog
(`FileDialog::Purpose::TagJson` — its own purpose so no other poller drains it).
A **modifier chord** is mandatory here: plain letters go to the filter field and
the `E` prompt. The picked file registers tag **categories** and stores per-tag
**descriptions** in the vault-global settings block; it tags no content. One
`set_vault_settings` commit for the whole file.

A **summary modal** then reports the outcome — categories added, descriptions
added, descriptions updated, always; plus entries skipped (malformed), entries
skipped (vault full), categories not registered (vault full), and fields
shortened, each only when non-zero. Any key dismisses it, and while it is up it
owns every key so the acknowledging keystroke cannot also act on the list behind
it. A failed commit shows the error line and **no** modal.

Note the overview lists tags something in the vault **carries**, so a description
imported for an unused tag is stored and safe but has no row until that tag is
applied — a direct consequence of this import deliberately tagging nothing.
User-facing JSON format reference:
`docs/superpowers/specs/2026-07-25-phase55-json-tag-dictionary-import-design.md`.

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

**Phase 56 redesign — two-line rows + id-based selection:**
- Every row, in every state (Queued, Running, Done, Failed, Cancelled), is **two lines**:
  1. **Route line** — `source_name → destination_gallery` (or `root` when destination is empty), always elided to fit.
  2. **Status line** — progress bar (for Running) with done/total text, or the outcome (✓ N imported M skipped / ✗ error / − Cancelled / Queued #id).
- The progress bar occupies only the status line, so bar and route text never overlap.
- **Selection is id-based, not positional.** `Ctrl+Up`/`Ctrl+Down` reorders the queued item
  and keeps focus on it, so the chord can be repeated on the same item. When a running import
  completes and reorders the list, focus stays on the selected task's new position.

### Footer bar (Phase 50)
Live summary while queue is non-empty: `"Importing <name> 128/450 · 2 queued"` (done/total, remainder queued).
**Priority:** error > import summary > status. Clickable to jump to ImportStatusScreen.

## Duplicate finder screen (Phase 61; waves + memory fix Phase 64)
`Ctrl+D` on the gallery grid (plain `D` toggles the detail panel) opens
`NavKind::ToDuplicates` — an exclusive op behind the same import-queue gate as
compact (`queue_.busy()` → "Imports running — press Shift+I" status). The grid's
`F1` popup gains a **"Vault tools"** group: `Shift+C — Compact vault`,
`Ctrl+D — Find duplicate files`.

Four screen states (the chooser is a state, not a modal dialog class):
1. **Choose:** "Exact duplicates" / "Exact + visually similar"; `Esc` leaves.
2. **Scanning:** progress (hashed/total candidates) + current name; `Esc`
   cancels gracefully between files. Files that fail to decrypt/decode are
   skipped and counted — the results show "couldn't examine N files".
   **Phase 62:** the "Exact + visually similar" mode also compares videos —
   duration gate (±2 %, 500 ms floor), poster dHash prefilter, then 5
   software-decoded frames at 10–90 % of the timeline (≥ 4 of 5 within
   Hamming ≤ 7). Matches group as **"Similar video"**; review/marking/apply
   are identical to other groups. Non-FFmpeg builds fall back to the
   duration+poster verdict.
3. **Review (Phase 64: wave-based):** groups sort largest-reclaimable-bytes
   first and present in **waves of ≤ 20 groups**; only the current wave is
   navigable and markable. Header: `Duplicates — wave 2/5 · 20 groups · 47
   files · 312 MB reclaimable · 61 groups in later waves`. Group header
   (`Identical · 3 files · 24.1 MB reclaimable` / `Similar (N bits)`), then a
   horizontal row of side-by-side member tiles (thumbnail or video poster;
   name, parent gallery path, size, resolution beneath) each carrying a
   KEEP/REMOVE badge — groups arrive pre-marked keep-first/remove-rest
   (Phase 63). Tiles share the full content width, centered, scaling down as
   the group grows; row heights are font-derived and the header/footer are
   opaque chrome bands (`ui/dup_layout.*`, pure/tested). Rendering is
   viewport-culled (Phase 64): off-screen rows neither draw nor fetch
   thumbnails, so review memory no longer scales with group count. Keys:
   `Left/Right` member focus, `Up/Down` group focus, `Space` toggle mark, `A`
   keep only the focused member, `Enter` full-screen inspect of the decoded
   original (any key returns), `N` skip the current wave (default-cancel
   confirm when user-touched marks exist; skipped files simply stay), `F1`
   help, `Esc` leave (confirm prompt only while USER-TOUCHED unapplied marks
   exist).
   **Group invariant:** a group with ALL members marked REMOVE renders in a
   warning state and blocks apply — that would be deletion, not de-duplication.
4. **Done:** reports totals accumulated across all applied waves ("Removed 37
   files (1.2 GB) in 3 waves · 1 wave skipped"; "No duplicates removed" when
   nothing was applied); grid refreshes via `on_vault_changed()`. `Enter`
   rescans and resets the totals + stale banner.

**Apply (per wave):** footer shows the current wave's running total ("12 files
marked · 96.3 MB") and the keybar `[Space] keep/remove  [A] keep only
[Ctrl+Enter] apply wave  [N] skip wave  [Esc] back`; `Ctrl+Enter` (deliberate
destructive chord) → default-cancel confirm with the wave's count + size → one
main-thread `vault::remove_media_batch` (N erases, ONE commit, one
auto-reclaim) → remaining groups' spans re-resolved from the index (Windows
auto-reclaim may compact and relocate chunks) → next wave. `blocks_idle_lock()`
is true while scanning or while the current wave has user-touched unapplied
marks; manual lock mid-scan cancels the worker before key wipe.

## Multi-volume archive confirm (Phase 53)
Picking any volume of a split set (`.7z.001`, `.z01`, `.partN.rar`, `.r00`, …)
auto-discovers its siblings from the containing directory and opens
`VolumeSetDialog` BEFORE anything is enqueued — importing a 40-part set is not
something to do silently off one click.
- **Body:** heading `"Import N volumes as one archive?"`, then the volume names
  in VOLUME order (not sorted — a spanned zip's `.zip` is last, old-style RAR's
  `.rar` is first). Long sets show the first 8 and elide the rest as
  `"... and N more"`; 7z happily emits 100+ parts and the panel must not grow
  past the window.
- **Keys:** `Enter`/`Y` confirm, `Esc`/`N` cancel.
- **A gap is a REFUSAL, not a warning to click past.** Unlike `ConsentDialog`,
  this dialog has a state where confirming is simply not offered: a missing
  volume yields a *corrupt* gallery, not a partial one. On an incomplete set the
  border turns `DANGER`, the warning names which ordinals are missing, `Enter`
  returns `Pending` (the dialog stays up so the user can read WHICH volume is
  gone), and the key hint drops to `"Esc - cancel"` — only the key that actually
  does something is offered.

### Lock confirm modal (Phase 50)
Manual lock, vault switch, or quit with pending queue: default-cancel modal reads **"N imports pending — finish current file, discard the rest, and lock?"**
On confirm (Y): current file completes, queue discarded, final commit-lane flush, passwords/keys wiped.
SDL_EVENT_QUIT also flows through this gate.
