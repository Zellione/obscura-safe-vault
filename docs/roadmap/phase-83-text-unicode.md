# Phase 83 — Every non-ASCII character in the UI rendered as nothing

## Owner report

> "in the F2 menu the are navigation hints that are basically empty []"

## Symptom

The settings overlay footer, authored as

```
[Tab] Switch  [↑↓] Move  [←→] Change  [Esc] Close
```

drew as

```
[Tab] Switch  [] Move  [] Change  [Esc] Close
```

The empty brackets are what made this one visible. The same defect was silently
eating characters across the whole app.

## Root cause

`gfx::FontAtlas` baked exactly one contiguous range — printable ASCII, bytes
32..126 — and both consumers **skipped every byte outside it**:

```cpp
// src/gfx/text.cpp, measure() and build_text_geometry()
if (ch < FIRST_GLYPH || ch >= FIRST_GLYPH + GLYPH_COUNT) continue;
```

Every byte of a UTF-8 multi-byte sequence is ≥ 0xC2, so no non-ASCII scalar
could ever draw. Because `measure()` skipped them too, layout stayed
self-consistent — nothing overflowed or misaligned, the characters were simply
absent, which is why this survived 2054 tests and 82 phases.

Scanning string literals across `src/` found **14 distinct codepoints in ~120
places**, all invisible:

| char | occurrences | example |
|---|---|---|
| `—` em dash | 45 | `"Imports running — press Shift+I for status"` |
| `·` middle dot | 28 | `"2 galleries · 7 images · 312 MB"` |
| `…` ellipsis | 26 | `"Working…"`, `"Compacting…"` |
| `↑ ↓ ← →` | 17 | settings/search nav hints, sort labels |
| `✓ ✗ − ★ • ▲ ▼` | 10 | import status, favourite marker, help scroll arrows |

The same skip also mangled **vault node names**, which are arbitrary user text
drawn straight through `draw_text`: `café.jpg` rendered as `caf.jpg`, and two
differently-named non-Latin files rendered identically blank.

## Fix — part 1: the atlas speaks UTF-8

`bake()` moves from `stbtt_BakeFontBitmap` (one contiguous range) to
`stbtt_PackBegin` / `stbtt_PackFontRanges` with an explicit codepoint array.
Coverage is declared as `EXTRA_RANGES` in `text.h` and **filtered against the
font's own cmap** at bake time via `stbtt_FindGlyphIndex`, so a block the
bundled font lacks costs nothing and can never pack a blank `.notdef` rect with
a bogus advance:

| block | why |
|---|---|
| `U+00A0–00FF` Latin-1 Supplement | `· × ± § µ` + accented Western European names |
| `U+0100–017F` Latin Extended-A | Central European names |
| `U+0180–024F` Latin Extended-B | |
| `U+0370–03FF` Greek | node names |
| `U+0400–04FF` Cyrillic | node names |
| `U+2010–2027` General Punctuation | `– — … • ‖` and quotes |
| `U+20A0–20BF` Currency | |
| `U+2212` | MINUS SIGN |

Storage keeps ASCII as a dense array indexed by subtraction (the hot path for
almost every string) and puts the rest in a codepoint-sorted `extra_` vector
searched with `std::ranges::lower_bound`.

Oversampling stays 1×1, so `stbtt_packedchar`'s `xoff` / `yoff` / `xadvance`
carry exactly the `stbtt_bakedchar` semantics this class was written against —
existing ASCII metrics are byte-for-byte unchanged.

`measure()` and `build_text_geometry()` now decode UTF-8. The decoder is
hardened because a vault node name is untrusted input that reaches `draw_text`
directly:

- every failure path consumes **exactly one byte** and returns `BAD_CODEPOINT`,
  bounding the loop at one glyph per byte — no input can hang it;
- overlongs, surrogates and out-of-range scalars are **rejected, not decoded**,
  so a `/` smuggled in as the overlong `C0 AF` cannot alias the `/` glyph.

Unmapped scalars and malformed bytes render `REPLACEMENT_GLYPH` (`?`) rather
than vanishing — one per scalar, not one per byte. Dropping them is what made
two differently-named CJK files render identically blank.

`text_top_for_center` deliberately still measures the ink extent over **ASCII
only**. `extra_` holds accented capitals and cedillas that reach further up and
down than any ASCII glyph; folding them in would have shifted the vertical
centring of every existing screen the moment coverage grew.

Atlas cost at the app's 28 px: 512×512 before → **1024×1024** after (1 MiB
bitmap, 4 MiB RGBA texture). One atlas exists process-wide.

## Fix — part 2: the glyphs the bundled font does not have

`assets/fonts/NotoSans-Regular.ttf` is a **subset** — 2965 codepoints: Latin,
Greek, Cyrillic and common punctuation. Parsing its cmap shows it carries
`— – … · • − × ± § µ ‖` but has **no** arrows, check marks, stars or geometric
shapes. Extending the atlas cannot conjure a glyph that is not in the font, so
those nine call sites are respelled in ASCII (each with a comment saying why, so
a future edit does not "restore" the nicer character and re-break it):

| site | before | after |
|---|---|---|
| `settings_overlay` footer | `[↑↓] Move  [←→] Change` | `[Up/Dn] Move  [Lt/Rt] Change` |
| `search_overlay` footer | `[↑↓] Navigate` | `[Up/Dn] Navigate` |
| `gallery_sort` labels | `Name ↑` / `Name ↓` | `Name asc` / `Name desc` |
| `detail_model` marker | `★ favorite` | `* favorite` |
| `help_popup` scroll | `▲` / `▼` | `^` / `v` |
| `import_status_row` route | `a.zip → root` | `a.zip -> root` |
| `import_status_row` status | `✓ …` / `✗ …` / `− Cancelled` | `Done — …` / `Failed — …` / `Cancelled` |

Everything else — all 99 occurrences of `— · … • − × ± § µ ‖` — is untouched and
now simply renders.

## Fix — part 3: the footer was never bounded

The settings footer hint moves into a pure `ui::settings_footer_hint(state)` in
`settings_model` and the draw site elides it with `ui::fit_text` against the
panel width, per the "any string in a fixed-width box must be elided" rule.

This was newly necessary: spelling the arrow keys out took the longest hint from
650 px to **760 px** at 28 px font — exactly the text budget of a maximum-width
(800 px) panel, and over it on any smaller window, where the panel is
`win_w - 80`. It was drawn raw before.

## Verification

Both reported symptoms were confirmed fixed against the running app under Xvfb:

- F2 footer now reads `[Tab] Switch  [Up/Dn] Move  [Lt/Rt] Change  [Esc] Close`
  in full — no empty brackets.
- Vault manager reads `No vaults yet — press N to create, O to open.` with the
  em dash present.

## Tests

`tests/gfx/test_text.cpp`
- `font_atlas_renders_ui_typography` — every non-ASCII codepoint the UI puts on
  screen measures > 0 and has a real glyph. **This is the guard**: a new
  typographic character the bundled subset cannot draw fails here rather than
  reaching a user as an invisible gap.
- `font_atlas_renders_accented_names` — Latin-1, Latin Extended-A, Greek and
  Cyrillic; `café.jpg` measures wider than `caf.jpg`.
- `font_atlas_measure_counts_utf8_scalars_not_bytes` — additivity across a
  mixed run.
- `font_atlas_unmapped_codepoint_draws_a_replacement` — CJK falls back to one
  `?` per scalar, not per byte.
- `font_atlas_malformed_utf8_terminates` — lone continuation byte, truncated
  sequence, overlong `C0 AF`, and `0xFF` each render a replacement.
- `build_text_geometry_emits_quad_for_non_ascii`
- `font_atlas_ascii_metrics_unchanged_by_extended_coverage`

`tests/ui/test_settings_model.cpp`
- `settings_footer_hint_is_free_of_unrenderable_glyphs` — every byte of every
  hint variant is printable ASCII.
- `settings_footer_hint_names_the_arrow_keys`
- `settings_footer_hint_prompt_state_wins_over_section`

Updated for the new wording: `test_import_status_row.cpp`,
`test_gallery_sort.cpp`.

## Format impact

None. No `.osv` change; `INDEX_VERSION` stays 12.
