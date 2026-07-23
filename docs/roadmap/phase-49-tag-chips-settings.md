## Phase 49 — Colour-coded tag chips & per-vault settings ✅

**Goal:** Stop rendering tags as raw `category:name` text. A tag whose prefix
matches a **configured category** is drawn as a chip — a small filled dot in the
category's colour plus the bare name — everywhere tags are shown. The
category→colour mapping is configured on a new **`F2` settings overlay**, which
also carries a **vault-wide default sort order** used by every gallery that has
not overridden it.

The mapping and the default sort are **per vault and stored inside the vault**,
encrypted like everything else: a vault's tag categories describe its contents,
so they must not sit in a plaintext config file. Only the UI theme — which
reveals nothing — stays in `config_dir()`.

### Design decisions (settled during brainstorming)

| Decision | Choice | Why |
|---|---|---|
| Where settings live | **In the vault** (index block), not `config_dir()` | Tag categories leak vault contents. |
| Theme | **Stays global** (`theme.conf`) | Reveals nothing; a machine-wide look is worth keeping. |
| Which prefixes are categories | **Only those configured by hand** | `12:30` and `Re:Zero` must never be mangled. |
| Colour picking | **Fixed 16-swatch palette**, stored as `u8` | No hex parsing, no unreadable combinations, 1 byte per row. |
| Chip style | **Dot + coloured text** (no pill) | Densest, closest to today's list, least draw code. |
| Settings layout | **Section sidebar + content pane** | Scales as settings grow. |
| Tiles | **Chip line under the label** (+ List rows) | Most informative while browsing. |
| "Not overridden" sort | **New explicit `Insertion` key**, byte 0 re-read as `Default` | Existing galleries adopt the default immediately, and a gallery can still be pinned to import order. |

### Tasks

#### Storage — `INDEX_VERSION` 7 → 8 (`src/vault/index.*`)

- [x] **Vault-global settings block**, serialised after the Phase 18
      saved-searches block (vault-level metadata, not a node):
      ```
      default_sort     u8    (SortKey; Insertion for pre-v8 blobs)
      tiles_show_tags  u8    (0/1;     1         for pre-v8 blobs)
      cat_count        u16   (≤ INDEX_MAX_TAG_CATEGORIES = 256)
      categories       { name_len u16; name u8[name_len]; swatch u8 } [cat_count]
      ```
      A `SortKey` out of range, a `tiles_show_tags` other than 0/1, or a
      `swatch ≥ TAG_SWATCH_COUNT` is **rejected on deserialise, not clamped** —
      the Phase 37 / Phase 47 rule. Category names are opaque UTF-8 bytes (a
      `.osv` is untrusted input), capped at `INDEX_MAX_CATEGORY_BYTES = 64` and
      case-insensitively de-duped on read, keeping the first casing.
- [x] **`SortKey` gains an eighth value.** Byte `0` is re-read as
      `Default` ("follow the vault's `default_sort`") and `Insertion = 7` is
      added for raw import order — today's `Manual` behaviour. Every existing
      gallery is byte 0, so all of them adopt the vault default the moment it is
      set, which is exactly "unless overwritten in the gallery". No migration
      pass and no rewrite of existing vaults on open.
- [x] **`VaultSettings` struct** + accessors in the Phase 37 free-friend shape:
      `vault_settings(const Vault&)` / `set_vault_settings(Vault&, VaultSettings)`,
      the setter persisting through the existing crash-safe `commit_index()`
      double-buffer swap. `serialize_index` / `deserialize_index` gain an
      overload taking the settings alongside `root` + `searches`; the existing
      two- and three-argument forms keep working (empty/default settings).

#### Tag → category resolution (`src/ui/tag_category.*`, pure)

- [x] Split a stored tag at the **first** `:`. If the prefix matches a configured
      category (case-insensitively), the chip shows the **suffix only** in that
      category's colour.
- [x] **Unconfigured prefix ⇒ verbatim.** The tag renders as its full stored
      text in `TEXT_DIM`, so `12:30`, `Re:Zero` and any category the user has not
      opted into are never mangled. An empty suffix (`"artist:"`) is likewise
      verbatim.
- [x] **Seed the category list** whenever a vault has no settings block — a
      freshly created vault *and* every pre-v8 vault — with the nhentai-style set
      `artist, character, parody, group, language, series, male, female`, each on
      a distinct swatch. Rows are freely added / renamed / removed; an empty
      saved list is a legitimate state and is **not** re-seeded.
- [x] **Storage and matching are unchanged.** Search, the advanced query,
      autosuggest, tag-overview counts and the tag editor's text input all keep
      operating on the full `"artist:Kaguya"` string. This phase is a *display*
      transform only.

#### Palette (`src/gfx/`)

- [x] 16 fixed swatches, persisted as a `u8` index and named for the settings UI
      ("Violet", "Teal", …). Because the chip paints the tag **text**, each
      swatch carries an **on-dark and an on-light RGB**; `gfx::tag_swatch(i)`
      picks between them by the active theme's background luminance, so every
      swatch stays legible on all four themes without a per-theme table.

#### Rendering (`src/ui/`)

- [x] Shared `ui::draw_tag_chips(...)` — a filled dot plus the name — used by
      every surface below, so chip geometry lives in exactly one place.
- [x] **Detail panel** (`detail_panel.*`, `detail_model.*`) — Tags and Inherited
      sections.
- [x] **Tag editor** (`tag_editor.*`) — own-tags list, the read-only inherited
      section, and the autosuggest dropdown. The user still *types* the full
      `artist:Kaguya`; only the display changes.
- [x] **Tag overview** (`tag_overview.*`) — rows. Accepted consequence:
      `female:glasses` and `male:glasses` both read "glasses", separated only by
      dot colour.
      **Scope cut during delivery (owner decision):** the *tag-galleries* and
      *tag-images* screen headers stay plain text. Neither file draws a header —
      both subclass `FavoritesScreen` and set a composed `title_` string that the
      shared base renders with a single `draw_text`, the same line that renders
      "Favorite Galleries"/"Favorite Images", which carry no tag at all. Chipping
      it meant reshaping a base class used by four subclasses for one title; a
      header is not a scannable list, so the value was not there.
- [x] **Grid tiles + List rows** (`gallery_grid.*`) — one chip
      line under the tile label, elided with a neutral `+N` counter on overflow;
      List rows get the chip run before the metadata column. **Own tags only** —
      inherited tags are identical for every tile in a gallery, so they are pure
      noise there.
      Geometry: the chip line is reserved **per gallery** — if no child in the
      current listing has a displayable chip, no space is reserved at all, so
      untagged vaults look exactly as they do today and rows never go ragged
      mid-gallery. `tiles_show_tags` turns the line off entirely.

#### Settings overlay (`src/ui/settings_overlay.*`, `src/app/app.cpp`)

- [x] **`F2` mirrors the `F1` help convention exactly:** `App` intercepts the key
      globally, owns one `SettingsState`, and draws the overlay on top of
      whichever screen is active. Deliberately **not** a `Screen` — a full screen
      would force `App` to reconstruct the screen the user came from on `Esc`,
      which is lossy (a paused video, a scroll position); the overlay preserves
      it for free.
- [x] **Sidebar + pane layout.** `Tab` moves between rail and pane, `↑↓` row,
      `←→` change value, `N` add category, `R` rename, `Del` remove, `Esc` close.
      - **Appearance — this machine.** Theme; `↑↓` previews live and persists via
        `platform::ThemePref`, exactly as the current picker does.
      - **Browsing — this vault.** Default sort order; show tags on tiles.
      - **Tag colours — this vault.** The category rows.
- [x] With **no vault unlocked**, the two vault-scoped sections render a
      `TEXT_FAINT` "Unlock a vault to configure" line; Appearance still works.
- [x] **`theme_picker.*` is deleted.** Its behaviour moves into the Appearance
      section, and `C` on the vault manager opens the settings overlay focused
      there — one theme UI, not two.
- [x] `help_groups()` / the `F1` popup gains the `F2` entry.

#### Sort integration (`src/ui/gallery_sort.*`, `gallery_grid.*`)

- [x] `next_sort_key` cycles all eight values:
      `Default → NameAsc → NameDesc → DateAsc → DateDesc → SizeAsc → SizeDesc →
      Insertion → Default`.
- [x] A new pure `effective_sort_key(gallery_key, vault_default)` resolves
      `Default` against the vault setting; `Vault::list` and the grid's
      breadcrumb/HUD both route through it.
- [x] `sort_key_label` returns empty for `Default` **only** when the vault
      default is `Insertion`, so an untouched vault's breadcrumb looks exactly as
      it does today; otherwise it shows the effective key.

#### Docs & memories

- [x] `ROADMAP.md` index row + the container-format section (v8 block, the
      `SortKey` change).
- [x] `mem:vault_format` (v8), `mem:module/vault`, `mem:module/ui`,
      `mem:module/gfx`, `mem:ui_spec` (chips, the `F2` overlay, the deleted theme
      picker), `mem:conventions` if the swatch table sets a new pattern.
- [x] `premake5.lua` — `tag_category.cpp` and `settings_overlay.cpp` (pure parts)
      added to the `osv_tests` file list.

**Out of scope (YAGNI):** free hex colour entry (the swatch is a `u8` index, so
adding it later means another format bump — deliberately deferred until there is
a reason); a category picker in the tag editor's add flow; per-category
visibility or filtering; nested categories; importing/exporting a category
mapping between vaults; per-vault theme overrides.

### Suggested delivery order

One phase, but naturally three parts (as in Phases 40/43/44) if the diff wants
splitting:

1. **Format + resolution** — the v8 block, `VaultSettings`, the `SortKey`
   change, `tag_category.*`, the swatch table. No visible UI yet.
2. **Chips** — `draw_tag_chips` and the six rendering surfaces.
3. **Settings overlay** — `F2`, the three sections, the `theme_picker.*`
   deletion, sort integration.

### Testing

- **Pure, unit-tested:** `tag_category.*` (split, ci lookup, verbatim fallback,
  empty suffix, no colon); settings-block serialise↔deserialise round-trip; every
  malformed-block rejection (bad `SortKey`, bad `tiles_show_tags`, `swatch` out
  of range, over-long category name, `cat_count` over the cap, truncation);
  `next_sort_key` over the eight-value cycle; `effective_sort_key`; chip-line
  elision and `+N` maths; the per-gallery reserve decision.
- **Fuzz:** the v8 settings block joins the existing index fuzz corpus.
- **Compatibility:** a v7 vault opens with the categories seeded, every gallery
  `Default`, `tiles_show_tags` on, and is written back as v8.
- **`scripts/test.sh --asan`** — required: this phase touches index
  deserialisation.

### Acceptance criterion

- A tag whose prefix matches a configured category renders as a coloured dot plus
  the bare name in the detail panel, tag editor, tag overview, tag
  galleries/images screens, grid tiles and List rows; a tag with no configured
  prefix renders verbatim.
- `F2` opens the settings overlay from every screen and `Esc` returns to it
  unchanged (a playing video keeps its position).
- Editing a category's colour is visible immediately and survives a lock/unlock
  cycle and an app restart; the mapping is present nowhere outside the `.osv`.
- Setting a vault default sort reorders every gallery still at `Default`, and a
  gallery pinned via `Shift+S` (including to `Insertion`) is unaffected.
- A vault written by Phase 48 opens without error, seeds the default categories,
  and round-trips as `INDEX_VERSION` 8.
- A hostile v8 blob with an out-of-range swatch, sort key, or `cat_count` is
  rejected rather than clamped.
- `scripts/test.sh` green; `scripts/test.sh --asan` clean.

### Completed work

Test count: **1048 → 1116**.

**Storage (`src/vault/`)**
- `index.*` — `INDEX_VERSION` **8**. `SortKey` reworked: byte `0` is re-read as
  `Default` ("follow the vault default") and `7 = Insertion` is added for raw
  import order, so existing galleries adopt the vault default with no migration.
  `read_node` bounds `sort_key` per version (v6/v7 max 6, v8 max 7). New
  `TagCategory{name, swatch}` and `VaultSettings{default_sort, tiles_show_tags,
  categories}` with `VaultSettings::seeded()` (8 nhentai-style categories on
  distinct swatches). Caps: `INDEX_MAX_TAG_CATEGORIES` 256,
  `INDEX_MAX_CATEGORY_BYTES` 64, `TAG_SWATCH_COUNT` 16. `write_settings` /
  `read_settings` mirror the saved-searches pair; the reader **rejects**
  out-of-range swatch/sort/flag bytes rather than clamping. `serialize_index` /
  `deserialize_index` gained 4-argument forms; the 2- and 3-argument ones delegate.
- `vault.*` / `index_io.*` — `Vault::settings_`, exposed through the
  `vault_settings(v)` / `set_vault_settings(v, s)` free friends (the `cpp:S1448`
  member-cap pattern). Persisted via `index_io::commit_index` — the main
  persistence path — and on the `unlock`/read and `compact()` paths; `reset()`
  clears it and `create()` seeds it. `Vault::list()` resolves each gallery's
  stored key against the vault default.

**Pure logic (`src/ui/`, `src/gfx/`)**
- `tag_category.*` (new) — `resolve_tag(tag, categories) -> TagDisplay{text,
  swatch}`. Splits at the **first** `:` and strips only when the prefix matches a
  configured category (case-insensitively, via the existing `ui::tag_ci_equal`)
  **and** the suffix is non-empty, so `12:30` and `Re:Zero` survive. `swatch < 0`
  means uncategorised. `TagDisplay::text` borrows from the caller's tag.
- `theme.*` (gfx) — `TAG_SWATCH_COUNT` 16, `tag_swatch(i)` (returns `Color` **by
  value**; picks an on-dark or on-light variant from background luminance) and
  `tag_swatch_name(i)`. A legibility test covers all 4 themes × 16 swatches.
- `tag_chip.*` (new) — `CHIP_DOT` 9 / `CHIP_GAP` 7 / `CHIP_SPACING` 12 /
  `CHIP_ROW_H` 16, defined here and nowhere else. `fit_chips`, `chip_width`,
  `lone_chip_text_w`, `pack_chip_lines` (+ `ChipLine` / `ChipWrap`),
  `any_chips_to_show`, and the shared `draw_tag_chips`. A `static_assert` ties
  `gfx::TAG_SWATCH_COUNT` to `vault::TAG_SWATCH_COUNT` — the two are deliberately
  separate constants because `gfx` must not depend on `vault`.
- `gallery_sort.*` — `effective_sort_key(gallery_key, vault_default)` (never
  returns `Default`), the full 8-value `Shift+S` cycle, `prev_sort_key` as its
  exact inverse, and a 2-argument `sort_key_label` that stays empty for an
  unconfigured vault so the breadcrumb looks pre-Phase-49.
- `settings_model.*` (new) — pure overlay state: the three-section rail, row
  navigation, value cycling and category CRUD (add/rename/remove with trimming,
  case-insensitive duplicate rejection and the persisted caps). SDL-free.

**Rendering**
- `detail_model.*` / `detail_panel.*` — `DetailSection::is_tags`;
  `draw_detail_panel` takes the category list and draws tag bullets as one-tag
  chip runs at `CHIP_ROW_H`. The model still stores **raw** tags: resolution is a
  draw-time concern.
- `tag_editor.cpp` — chips at all three sites (current-tags rows, the read-only
  inherited section wrapped across up to 3 lines with a right-aligned `+N`, and
  the autosuggest dropdown). Display only: the input buffer, `all_tags` and
  `editor_tag_suggestions` all still work on the full `artist:Kaguya` string. The
  per-row `[Delete]` hint was dropped (the modal footer already reads
  `[Del] Remove`); `pack_tag_lines` was replaced by the unit-tested
  `pack_chip_lines`.
- `tag_overview.cpp` — chip rows; the count column keeps its exact x.
- `gallery_grid.cpp` — chips on grid tiles and List rows. **The cell does not
  grow**: `GridSpec` is a shared, explicitly-square abstraction driving both
  `grid_cell_rect` and `grid_hit` for this screen *and* `favorites_screen`, so
  instead the label moves up by `CHIP_ROW_H` and the thumbnail shrinks by the
  same 16 px. Every layout metric — `grid_columns`, `grid_visible_range`,
  `content_height`, the scroll step and `hit_test` — is untouched, which makes
  the drawing-vs-hit-testing divergence class structurally impossible here.

**Settings overlay**
- `settings_overlay.*` (new) — veil + section rail + row pane + footer, modelled
  on the `F1` help popup. Live theme preview and persistence identical to the
  retired picker; inline text prompt for adding and renaming categories; failures
  surface in the overlay's own error line.
- `app.*` — owns `ui::SettingsState`, intercepts `F2` globally, and seeds the
  overlay through a single `open_settings_overlay()` helper. Checked **after**
  the `help_.open` guard so the help popup, which draws on top, keeps its keys
  when both are open. `NavKind::ToSettings` is excluded from `apply_nav`'s screen
  teardown, so opening settings never rebuilds the screen behind it.
- `help_popup.cpp` — synthesises a global `F1`/`F2` group, since `help_groups()`
  is per-screen and had no shared entry point.
- `theme_picker.*` **deleted**; `C` on the vault manager now emits
  `NavKind::ToSettings`.

**Status:** ✅ Shipped.

`scripts/test.sh` 1117 tests / 0 failed; `scripts/test.sh --asan` 0 failed with no
LeakSanitizer report.

**Post-delivery fix — a latent ODR violation this phase's tests exposed.** The `--asan`
gate failed with "3770 byte(s) leaked in 14 allocation(s)", all of it `vault::Vault`
members, all attributed to `tests/vault/test_vault_settings.cpp`. Root cause was not in
this phase's code at all: **six vault test files each declared their own `TempVault` at
namespace scope with six different layouts.** Member functions defined inside a class are
implicitly inline, so those are weak symbols — the linker keeps exactly one
`TempVault::~TempVault()` and silently discards the rest, then calls the survivor for
every file's objects. This phase added the first `TempVault` to hold a `vault::Vault`
member, so the winning destructor had no such member and that Vault was never destroyed.
Fixed by giving every `TempVault` internal linkage. An audit then found 15 more such
definitions across `tests/vault`, `tests/ui` and `tests/image` that were not leaking only
because the surviving destructor happened to suit their layouts; those were swept too
(owner decision), so `grep -rl "struct TempVault" tests/` now reports internal linkage
everywhere and the hazard class is gone rather than just this instance of it.

Also fixed while investigating: `Vault`'s **move constructor and move assignment both
omitted `settings_`**, so moving a `Vault` silently dropped its vault-global settings —
a real Phase 49 bug from Task 3. Regression test `vault_settings_survive_move`.
