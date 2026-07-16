## Phase 36 — Robust special-character filename & archive-name handling ✅

**Goal:** Archive entries and archive filenames containing non-ASCII text,
legacy (non-UTF-8) encodings, filesystem-illegal characters, or path-
traversal sequences import (and later export) correctly instead of mojibake,
a crash, or a rejected import — across every archive backend (Phase 17/24/34).

**Part 1 (shipped) — the security half.** Investigating two SonarCloud `cpp:S2083`
BLOCKERs found a *reachable* path traversal that this phase had only anticipated
for archives: a node name is deserialised from the vault index with no validation
at all (`index.cpp`'s `read_string`), `Vault::add_image` only checked it was
non-empty, and Export built its output path as `dest_dir / name` — and
`std::filesystem::path::operator/` does **not** contain. An absolute name
(`"/etc/cron.d/x"`) discards `dest_dir` outright; a relative one (`"../../.bashrc"`)
walks out of it. Since a `.osv` vault is a portable, shareable artifact (vault
manager, cross-vault transfer), a hostile vault was an arbitrary-file-write
primitive on export. Fixed at three layers — see the tasks below.

### Tasks
- [x] **Legacy zip encoding fallback** — a zip/cbz entry without the UTF-8
  language-encoding flag (bit 11, `mz_zip_archive_file_stat::m_bit_flag`) is
  decoded through `ui::decode_zip_entry_name` (`src/ui/zip_encoding.*`, pure +
  unit-tested): a fixed 128-entry CP437->Unicode table for bytes 0x80-0xFF
  (bytes 0x00-0x7F are ASCII-identical), the overwhelmingly common legacy
  encoding for zip tools that predate the flag. A name that happens to already
  be valid UTF-8 without the flag (some tools write UTF-8 but never set it) is
  detected and passed through unchanged rather than mis-decoded as CP437
  mojibake. **Shift_JIS is explicitly out of scope** (see below) — such a name
  still imports safely, just decoded (incorrectly) as CP437 rather than
  crashing or blocking the import.
- [x] **Filesystem-illegal / control characters** — `vault::is_safe_node_name` /
  `vault::sanitize_node_name` (`src/vault/safe_name.*`, pure + unit-tested) are
  the one shared rule: control bytes/NUL/DEL, the Windows-reserved set
  (`< > : " / \ | ? *`), reserved device names (`CON`, `NUL`, `COM1`–`COM9`, …),
  and trailing dots/spaces. Applied to names from archive entries, `meta.json`
  titles (replacing the old one-off `'/' → '_'`), and picked files. Tags stay
  free-form UTF-8 — they are not filesystem paths.
- [x] **Path-traversal guard** — enforced at three layers, not one:
  *ingress* (`Vault::add_image` / `add_video` / `create_gallery` **reject** an
  unsafe name — the vault API is the trust boundary); *importers* (**repair** via
  `sanitize_node_name`, so an awkward archive name never fails a whole import);
  and the *export sink* (`ui::export_path_within` normalizes with
  `weakly_canonical` and confirms containment with `lexically_relative`, so even a
  vault already on disk with a hostile name cannot write outside the chosen
  folder). Fails closed.
- [x] **Long-name handling** — names are capped at `MAX_NODE_NAME_BYTES` (255) and
  truncated on a **UTF-8 codepoint boundary** so a multi-byte character is never
  torn in half. The CBZ basename-collision disambiguation is preserved (and its
  counter moved ahead of the prefix, so truncation can never eat it and the
  dedupe loop is guaranteed to terminate).
- [x] **Platform path boundary** — `platform::normalize_user_path` normalizes every
  externally-supplied path (native file-dialog results, lines read back from
  `vaults.list`) before it can reach `fopen`: rejects empty / embedded-NUL /
  over-long, and collapses `..` and `.` components. It deliberately does *not*
  confine paths to a base directory — users legitimately keep vaults on arbitrary
  drives and removable media.
- [x] Update `CLAUDE.md` / `mem:core`.
- [x] `tests/` — entries with `../`, `..\`, absolute paths, control characters,
  Windows-reserved characters/names, embedded NULs and >255-byte names all import
  safely; `sanitize_node_name`'s output is asserted to always satisfy
  `is_safe_node_name`; forged hostile index nodes (relative **and** absolute) are
  proven unable to write outside the export folder; existing UTF-8 fixtures and
  the CBZ disambiguation are unchanged.
- [x] `tests/` — `tests/ui/test_zip_encoding.cpp` covers the UTF-8-flag
  pass-through, already-valid-UTF-8-without-the-flag detection, and known
  CP437 byte->codepoint mappings (accented Latin letters, box-drawing/symbol
  bytes); `tests/ui/test_zip_import.cpp` adds an end-to-end fixture (a zip
  entry written with `MZ_ZIP_FLAG_ASCII_FILENAME` so the UTF-8 bit is unset,
  holding a raw CP437 byte in its name) proving `import_zip` stores the
  correctly-decoded UTF-8 name, not the raw byte or a mis-decode.

**Out of scope (YAGNI):** Shift_JIS / other double-byte legacy encodings — a
correct decoder needs a JIS X 0208-sized table (or a new dependency on
libarchive's own string-conversion routines just for this one path); a
Shift_JIS-named entry without the UTF-8 flag still imports safely today, just
mis-decoded as CP437 rather than crashing or blocking the import. A
user-facing encoding picker (auto-detect only); renaming already-imported
vault content; changing how tags are stored (free-form UTF-8 tags are
unaffected).

### Acceptance criterion
Zip/cbz/7z/rar/tar fixtures with CP437 names, path-traversal attempts,
filesystem-illegal characters, and very long names all import safely with
correctly readable (or safely sanitized) names; no crash, no escape outside
the planned gallery tree, and existing plain-UTF-8 imports are unchanged.
(Shift_JIS names still import safely but are not correctly decoded — see
Out of scope.)

**Status:** ✅ Shipped — Part 1 (sanitization, three-layer traversal guard,
long-name truncation, platform path normalization) and Part 2 (CP437 legacy
zip-encoding fallback) both complete.
