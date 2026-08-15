# Phase 84 — Import perf, persisted gallery view, Home/End, crash-dump docs

## Overview

Four independent features: archive import performance (O(n²) → O(n) fix), persisted default gallery view, keyboard navigation polish, and documentation of Windows crash-dump security implications.

---

## 84.1 — Recursive archive import performance

### Root cause: O(n²) decompression

Phase 53 introduced recursive archive import (nested 7z, zip, rar, tar) via a stateless hook architecture: each call to `arc_list()` and `arc_extract()` in `recursive_hooks.cpp` created a fresh `ArchiveReader` and called `open()`, which for solid archives means a **full forward re-scan from the beginning** to find the target entry.

In a recursive import, this manifested as:
- Importing a 7z-in-7z with N total entries: O(N) entries in the outer archive trigger O(N) reader opens
- The inner archive extraction: O(N²) decompression total (solid archive re-scanned per entry)

Measured: a nested solid archive with 150 entries paid N reader opens + O(N²) decompression bytes; the first entry decompressed all preceding frames, the second decompressed even more, etc.

### Solution: ArchiveReaderCache

`ui::ArchiveReaderCache` (src/ui/archive_reader_cache.{h,cpp}) holds one `ArchiveReader` per live archive buffer, keyed by (data pointer, size). The reader's forward stream cursor rides across multiple entry extractions — a single archive now costs one open no matter how many entries are extracted.

**Lifecycle guarantee:** The cache assumes the buffer lives long enough for the walker to extract and drop it before reusing the buffer address. Phase 84's new `RecursiveHooks::archive_done` frame-pop hook ensures this: it drops the reader **before** the buffer is freed, so a later memory allocation cannot alias a stale cached reader.

**Scaling:** The same import now costs O(archives) opens, not O(entries). For the nested 7z example: 2 opens (outer + inner) instead of N opens + O(N²) decompression.

### Measurement methodology

Test setup:
- Inner archive: 150 small files (~256 KB each) in a solid 7z
- Outer archive: the inner 7z as a single entry in another solid 7z
- Harness: `ArchiveReaderCache` with nested extraction; counts `opens()`

Observable: cache.opens() == 2 for the nested case (outer, then inner), confirming O(archives) cost.

**Note:** The harness was run as a temporary test (test_archive_cache_perf.cpp) during Phase 84 implementation and deleted before committing. The structural observable (opens count) is covered by regression tests in `test_archive_reader_cache.cpp`.

---

## 84.2 — Persisted default gallery view

### Feature

The gallery grid supports five view modes (List/Grid S/M/L/XL, introduced in Phase 40), but the choice was session-only. Phase 84 persists the selection per vault and seeds it at app startup.

**Persistence layer:**
- New `platform::GalleryViewPref` struct (following the `ThemePref` pattern from Phase 49)
- File: `gallery_view.conf` in the app's config directory
- Read at app start; written on every L-key press + setting change

**UI signals:**
- **L-key** now shows "View: <label>" (Grid, List, Grid S/M/L/XL) in the gallery grid footer, and saves to config
- **F2 settings** gain an "Appearance" row: "Default Gallery View" dropdown (1 + 5 options = 6 total), synced to the session's current grid and to config
- The App seeds the grid's initial view from the saved preference at startup and after `promote_pending()` (end of import)

**Helpers:**
- `ui::gallery_view_label(view)` → "Grid M"
- `ui::gallery_view_slug(view)` → "grid_m" (for persistence)
- `ui::gallery_view_from_slug(slug)` → `GalleryView` (reverse)
- `ui::prev_gallery_view(current)` → previous in the cycle

---

## 84.3 — Home/End navigation

### Feature

The **Home** and **End** keys now jump to the first and last item in the current gallery grid, respectively, with automatic scroll-following (ScrollFollow::Center).

**Implementation:**
- `GalleryGrid::handle_home_key()` / `handle_end_key()` + `NavModel::clamp_to_bounds()` to respect the grid's current width
- F1 help popup gains entries under the "Navigation" group: `[Home/End] Jump to first/last`

**Tests:** NavModel clamp behavior verified in unit tests; grid navigation tested in integration.

---

## 84.4 — Windows crash-dump documentation

### Feature

New README section: **"Windows Crash Dumps (WER LocalDumps)"** documenting:

1. **Setup:** How to enable Windows Error Reporting (WER) LocalDumps registry key to capture minidumps
2. **Security caveat:** Crash dumps are binary memory snapshots. On Windows, `SetProcessWorkingSetSize` raises the `VirtualLock` budget (to work around page-file-size limits), but `VirtualLock` does NOT prevent hibernation (`hiberfil.sys`) from capturing mlock'd pages. A crash dump can include decrypted image pixel data if:
   - A vault is unlocked at crash time
   - Images are actively being decoded/displayed (in the texture cache or being zoomed)
3. **Mitigation:** Linux already uses `prctl(PR_SET_DUMPABLE, 0)` + `setrlimit(RLIMIT_CORE, 0)` in Release builds to disable core dumps entirely. Windows has no equivalent system-level disable; users who need protection should:
   - Lock the vault before hibernation/sleep
   - Run in Debug builds (if available) where dumps are explicitly enabled for debugging
   - Or disable WER LocalDumps for this application

The section explains the threat model (local attacker with disk access) and clarifies that the phase's other hardening (constant-time crypto, key wiping, `crypto_wipe`) is unaffected — crash dumps are a *platform* I/O vector, not a code defect.

---

## Summary

| item | what changed |
|---|---|
| perf | O(n²) → O(n) reader opens in nested archive import; cache ties reader lifetime to buffer via frame-pop hook |
| ux | persisted gallery view + F2 sync, Home/End keys |
| docs | Windows crash-dump security model documented |
| tests | 2081 tests / 0 failed; ASAN clean |

No `.osv` format change; `INDEX_VERSION` stays 12.
