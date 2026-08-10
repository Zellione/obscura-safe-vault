# Phase 72 — Unicode dialog paths & CJK archive entry names

**Status: ✅ shipped**

## Problem

Phase 70 ("Unicode-safe paths") fixed the Windows import crash class — but the
owner reproduced it again: importing archives or plain files still crashed the
program whenever the **archive name or the filenames inside the archive**
contained Japanese or Chinese characters.

Investigation found two independent bugs behind the one report.

### Bug 1 — the dialog callbacks themselves were missed (the crash)

`FileDialog::on_files` / `FolderDialog::on_folder` — the self-described
"single choke point through which every externally-chosen path enters the
program" — still rendered every picked path with `norm->string()`. On Windows
that converts the native UTF-16 path through the ANSI code page and **throws
`std::system_error`** when a character has no mapping (any CJK name on a
Western code page). Thrown inside the SDL dialog callback, the exception is
unhandled ⇒ the app dies on the very pick that was supposed to start the
import. This is exactly the conversion `path_utf8.h`'s header comment bans;
Phase 70 swept every consumer but not the choke point itself.

Several consumers also still re-decoded the stored UTF-8 strings through the
narrow `fs::path` constructor (ANSI on Windows, throwing on DBCS code pages).

### Bug 2 — CJK entry names inside 7z/RAR archives collapsed to "unnamed"

libarchive converts 7z/RAR entry names (stored as UTF-16 in the archive
header) through the **current locale at parse time**. The process never called
`setlocale`, so it ran under the default `"C"` locale, where that conversion
fails for every non-ASCII name — all accessors (`archive_entry_pathname`,
`_w`, `_utf8`) return NULL, and `ArchiveReader::scan_entries` collapsed each
CJK entry to the `sanitize_node_name` "unnamed" fallback. Zip/CBZ (miniz) was
unaffected: it reads raw name bytes and `decode_zip_entry_name` handles them.

## Fix

### Dialog paths (Bug 1)

- **`platform::normalize_external_path_utf8`** (paths.h): `normalize_user_path`
  → `path_to_utf8`. The one sanctioned dialog→`std::string` conversion; both
  dialog callbacks now use it. (Project convention: a `std::string` holding a
  path is UTF-8 by definition.)
- **Consumer sweep** of remaining narrow conversions of dialog strings:
  `gallery_grid.cpp` (file-import list, single-archive naming, folder-import
  pending path, export destination), `export_ui.cpp`, `unlock_screen.cpp`
  (vault path, new/read keyfile), `vault_unlock_picker.cpp` (keyfile),
  `collection_ops.cpp` (export dest), `image_viewer.cpp` (export status via
  `path_to_utf8`), and `paths.cpp` `config_dir` (SDL_GetPrefPath is UTF-8 —
  matters for CJK Windows usernames).

### Archive entry names (Bug 2)

- **`platform::init_locale()`** (`platform/locale_init.h`): switches
  **LC_CTYPE only** (never LC_NUMERIC — decimal-comma corruption) to a UTF-8
  locale at startup: env locale on POSIX with a `C.UTF-8` fallback. Called
  from `app/main.cpp` and `tests/test_main.cpp` before any threads exist.
  **Deliberate no-op on Windows**: libarchive keeps the wide name there
  regardless (the `entry_name_utf8` fallback), and a non-"C" CRT locale makes
  libarchive build an OEM(CP437)→locale conversion for tar names that mangles
  raw UTF-8 bytes into valid-but-wrong UTF-8 (caught by
  `archive_import_tar_cjk_entry_names` on Windows CI).
- **`ArchiveReader` `entry_name_utf8`**: narrow name if it is valid UTF-8
  (authoritative for raw-byte formats like tar — byte-identical pass-through),
  else the **wide** name converted locale-independently via
  `std::filesystem::path` (the Windows path: libarchive on Win32 always keeps
  the wide form, `AES_SET_WCS`, even when the ACP conversion fails), else the
  raw narrow bytes (legacy locally-encoded tar; `sanitize_node_name` repairs
  downstream).
- **`ui::is_valid_utf8`** exposed from `zip_encoding` (was file-local).
- **Multi-volume opens** use `archive_read_open_filenames_w` on Windows —
  the narrow variant reaches `fopen` through the ANSI code page and cannot
  open a CJK volume path.

### Tests

- `normalize_external_path_utf8` CJK round-trip + rejection tests
  (`test_paths.cpp`).
- End-to-end zip import with CJK archive filename **and** CJK entry names
  (`test_zip_import.cpp`).
- libarchive: 7z CJK entry names from a **committed real-7-Zip fixture**
  (`tests/ui/fixtures/cjk_names.7z.uu`, uuencoded like the RAR multivolume
  fixtures — libarchive's own 7z *writer* converts names through the locale
  and cannot produce CJK names under `"C"`, so the fixture cannot be built at
  test time); tar CJK entry names (raw-byte round-trip); CJK archive
  *filename* (`test_archive_import.cpp`).

## Acceptance criterion

Importing an archive whose own filename and entry names are Japanese/Chinese
succeeds on Linux and Windows with names preserved byte-identically as vault
node names; picking any CJK-named file in any dialog never crashes.

1929 tests / 0 failed; ASAN clean.
