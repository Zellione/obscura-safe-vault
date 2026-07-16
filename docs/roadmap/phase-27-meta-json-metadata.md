## Phase 27 — `meta.json` metadata on archive import 🔜

**Goal:** When a zip/cbz archive contains a top-level `meta.json`, use it to **title**
and **tag** the imported gallery instead of falling back to the filename.

Expected shape (all fields optional; unknown keys ignored):

```json
{
  "title": { "english": "English Title", "japanese": "日本語タイトル" },
  "tags":  [ { "type": "tag", "name": "awesome tag" },
             { "type": "artist", "name": "someone" } ]
}
```

### Tasks
- [x] **Parser** — a pure, SDL/vault-free `meta.json` parser (`src/ui/meta_json.{h,cpp}`) → `{ title_english, title_japanese, tags[] }`. Needs a JSON reader (the project has none today): **vendor a single-header MIT JSON lib** (e.g. `nlohmann/json`) rather than hand-rolling UTF-8/escape handling — decision confirmed: **nlohmann/json v3.12.0** vendored as `vendor/json`, used exception-free (`parse(..., allow_exceptions=false)`). Tolerant of missing/partial fields; ignores unknown keys.
- [x] **Mapping** (agreed decisions):
  - Gallery **name** = `title.english`, falling back to `title.japanese`, then the archive filename.
  - `title.japanese`, when present, is added as a **tag** (so it stays searchable).
  - Each `tags[]` entry becomes a **type-prefixed** tag `"<type>:<name>"` (e.g. `artist:someone`, `tag:awesome tag`), applied through the existing `Vault::add_tag` merge (case-insensitive de-dupe).
- [x] **Wire into import** — `build_zip_plan` / `build_cbz_plan` + `import_zip` / `import_cbz` detect a top-level `meta.json`; if present it overrides the default gallery name (the top gallery for zip, the single leaf for cbz) and seeds that gallery's tags. `meta.json` itself is **not** imported as a page. No `meta.json` → today's behaviour is byte-for-byte unchanged.
- [x] Update `CLAUDE.md` / `mem:core`.
- [x] `tests/` — a fixture archive with `meta.json` imports under the english title, tagged with the japanese title and each `type:name`; a missing / partial / malformed `meta.json` degrades gracefully to a filename-named, untagged import.

**Out of scope (YAGNI):** PDF metadata (PDFs carry no archive `meta.json` — a later phase could read the PDF info dict); writing `meta.json` back out; nested/per-folder `meta.json`; acting on fields beyond title + tags (the parser ignores extras without error).

### Acceptance criterion
Importing a fixture zip/cbz containing `meta.json` yields a gallery named after the
english title, tagged with the japanese title and each `type:name` tag; a malformed
`meta.json` never blocks the import.

**Status:** ✅ 677/677 tests pass; `scripts/test.sh --asan` clean. `ui::parse_meta_json` (nlohmann/json v3.12.0, vendored `vendor/json`, exception-free parse) + `meta_gallery_name`/`meta_gallery_tags` mapping; `find_meta_entry` excludes the top-level `meta.json` from all three planner paths (never placed, never counted skipped); `import_zip` (NewGallery) and `import_cbz` seed the created gallery's tags via `Vault::add_tag`; Append only excludes the file. Extraction goes to mlock'd memory with a 1 MiB sanity cap; a malformed `meta.json` degrades to the filename-named, untagged import.

**Follow-up (owner feedback):** the meta title no longer silently overrides the
name typed in the popup. Instead `ui::peek_archive_meta` reads the archive's
`meta.json` when the file is picked and the gallery-name popup is **prefilled**
with `meta_gallery_name(meta, filename-stem)` — the text the user confirms is
authoritative for the import (zip NewGallery and cbz alike). Tag seeding is
unchanged.

**Follow-up 2 (owner feedback):**
- The generic tag type `tag`/`tags` (case-insensitive) no longer gets a prefix —
  `{"type":"tag","name":"ponytail"}` imports as `ponytail`, not `tag:ponytail`.
  Real types (`artist:`, `character:`, `parody:`, …) keep their prefix.
- The tag editor now shows the ancestor-gallery tag cascade: a read-only
  "Inherited from gallery" section (pure `ui::inherited_tags`, new
  `src/ui/tag_inherit.{h,cpp}`) below the own-tags list, so meta.json tags on a
  gallery are visible when a page's editor is opened. Del/selection only ever
  touch the node's own tags.
