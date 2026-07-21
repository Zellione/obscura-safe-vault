## Phase 49 — Colour-coded tag chips & per-vault settings ⬜

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

- [ ] **Vault-global settings block**, serialised after the Phase 18
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
- [ ] **`SortKey` gains an eighth value.** Byte `0` is re-read as
      `Default` ("follow the vault's `default_sort`") and `Insertion = 7` is
      added for raw import order — today's `Manual` behaviour. Every existing
      gallery is byte 0, so all of them adopt the vault default the moment it is
      set, which is exactly "unless overwritten in the gallery". No migration
      pass and no rewrite of existing vaults on open.
- [ ] **`VaultSettings` struct** + accessors in the Phase 37 free-friend shape:
      `vault_settings(const Vault&)` / `set_vault_settings(Vault&, VaultSettings)`,
      the setter persisting through the existing crash-safe `commit_index()`
      double-buffer swap. `serialize_index` / `deserialize_index` gain an
      overload taking the settings alongside `root` + `searches`; the existing
      two- and three-argument forms keep working (empty/default settings).

#### Tag → category resolution (`src/ui/tag_category.*`, pure)

- [ ] Split a stored tag at the **first** `:`. If the prefix matches a configured
      category (case-insensitively), the chip shows the **suffix only** in that
      category's colour.
- [ ] **Unconfigured prefix ⇒ verbatim.** The tag renders as its full stored
      text in `TEXT_DIM`, so `12:30`, `Re:Zero` and any category the user has not
      opted into are never mangled. An empty suffix (`"artist:"`) is likewise
      verbatim.
- [ ] **Seed the category list** whenever a vault has no settings block — a
      freshly created vault *and* every pre-v8 vault — with the nhentai-style set
      `artist, character, parody, group, language, series, male, female`, each on
      a distinct swatch. Rows are freely added / renamed / removed; an empty
      saved list is a legitimate state and is **not** re-seeded.
- [ ] **Storage and matching are unchanged.** Search, the advanced query,
      autosuggest, tag-overview counts and the tag editor's text input all keep
      operating on the full `"artist:Kaguya"` string. This phase is a *display*
      transform only.

#### Palette (`src/gfx/`)

- [ ] 16 fixed swatches, persisted as a `u8` index and named for the settings UI
      ("Violet", "Teal", …). Because the chip paints the tag **text**, each
      swatch carries an **on-dark and an on-light RGB**; `gfx::tag_swatch(i)`
      picks between them by the active theme's background luminance, so every
      swatch stays legible on all four themes without a per-theme table.

#### Rendering (`src/ui/`)

- [ ] Shared `ui::draw_tag_chips(...)` — a filled dot plus the name — used by
      every surface below, so chip geometry lives in exactly one place.
- [ ] **Detail panel** (`detail_panel.*`, `detail_model.*`) — Tags and Inherited
      sections.
- [ ] **Tag editor** (`tag_editor.*`) — own-tags list, the read-only inherited
      section, and the autosuggest dropdown. The user still *types* the full
      `artist:Kaguya`; only the display changes.
- [ ] **Tag overview / tag-galleries / tag-images** (`tag_overview.*`,
      `tag_galleries.*`, `tag_images.*`) — rows and screen headers. Accepted
      consequence: `female:glasses` and `male:glasses` both read "glasses",
      separated only by dot colour.
- [ ] **Grid tiles + List rows** (`gallery_grid.*`, `tile_thumb.*`) — one chip
      line under the tile label, elided with a neutral `+N` counter on overflow;
      List rows get the chip run before the metadata column. **Own tags only** —
      inherited tags are identical for every tile in a gallery, so they are pure
      noise there.
      Geometry: the chip line is reserved **per gallery** — if no child in the
      current listing has a displayable chip, no space is reserved at all, so
      untagged vaults look exactly as they do today and rows never go ragged
      mid-gallery. `tiles_show_tags` turns the line off entirely.

#### Settings overlay (`src/ui/settings_overlay.*`, `src/app/app.cpp`)

- [ ] **`F2` mirrors the `F1` help convention exactly:** `App` intercepts the key
      globally, owns one `SettingsState`, and draws the overlay on top of
      whichever screen is active. Deliberately **not** a `Screen` — a full screen
      would force `App` to reconstruct the screen the user came from on `Esc`,
      which is lossy (a paused video, a scroll position); the overlay preserves
      it for free.
- [ ] **Sidebar + pane layout.** `Tab` moves between rail and pane, `↑↓` row,
      `←→` change value, `N` add category, `R` rename, `Del` remove, `Esc` close.
      - **Appearance — this machine.** Theme; `↑↓` previews live and persists via
        `platform::ThemePref`, exactly as the current picker does.
      - **Browsing — this vault.** Default sort order; show tags on tiles.
      - **Tag colours — this vault.** The category rows.
- [ ] With **no vault unlocked**, the two vault-scoped sections render a
      `TEXT_FAINT` "Unlock a vault to configure" line; Appearance still works.
- [ ] **`theme_picker.*` is deleted.** Its behaviour moves into the Appearance
      section, and `C` on the vault manager opens the settings overlay focused
      there — one theme UI, not two.
- [ ] `help_groups()` / the `F1` popup gains the `F2` entry.

#### Sort integration (`src/ui/gallery_sort.*`, `gallery_grid.*`)

- [ ] `next_sort_key` cycles all eight values:
      `Default → NameAsc → NameDesc → DateAsc → DateDesc → SizeAsc → SizeDesc →
      Insertion → Default`.
- [ ] A new pure `effective_sort_key(gallery_key, vault_default)` resolves
      `Default` against the vault setting; `Vault::list` and the grid's
      breadcrumb/HUD both route through it.
- [ ] `sort_key_label` returns empty for `Default` **only** when the vault
      default is `Insertion`, so an untouched vault's breadcrumb looks exactly as
      it does today; otherwise it shows the effective key.

#### Docs & memories

- [ ] `ROADMAP.md` index row + the container-format section (v8 block, the
      `SortKey` change).
- [ ] `mem:vault_format` (v8), `mem:module/vault`, `mem:module/ui`,
      `mem:module/gfx`, `mem:ui_spec` (chips, the `F2` overlay, the deleted theme
      picker), `mem:conventions` if the swatch table sets a new pattern.
- [ ] `premake5.lua` — `tag_category.cpp` and `settings_overlay.cpp` (pure parts)
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

**Status:** ⬜ Not started.
