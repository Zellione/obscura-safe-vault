## Phase 58 — Big-vault responsiveness & import-time UI stability ✅

**Goal:** Eliminate render-thread vault I/O and per-frame state thrashing that causes
0.5–1 s UI latency on large vaults (200 GB / 50k+ items). Keep playback, scroll and
selection state stable during batch imports. Add optional perf instrumentation for
before/after profiling.

**Root causes:**
1. **Render-thread I/O:** tile_thumb cache misses did synchronous seek+read+decrypt per frame;
   cover spans were re-resolved per frame; failed reads retried every frame.
2. **Per-keystroke index walk:** search overlay re-walked the entire vault index on every keystroke.
3. **Drain-time state loss:** grid `on_vault_changed` cleared selection on every batch commit;
   collection viewers (favorites, tag overview, search results) held dangling `IndexNode*` pointers
   across drains.

### Completed work

- **`DecodeWorker::submit_fetch`** — new two-stage pipeline. Worker thread calls a
  `std::function<bool(crypto::SecureBytes&)>` Fetcher before decode, returning empty
  Result (memoized as failed) on false. `image/` stays vault-agnostic; hosts wire the
  fetcher to their vault and cache strategy.

- **Vault `thumb_fp_` + `thumb_mutex_`** — third dedicated background file handle for
  thread-safe `read_thumbnail` / `read_thumb_span` from any thread. `reset()` flips
  `unlocked_` and closes the handle UNDER the mutex BEFORE key wipe (quiesce-before-wipe
  ordering). `read_fp_` stays main-thread-only. `resolve_node` promoted public.

- **`ThumbKey{key, offset, length, present}`** — cache identity (key) unchanged; offset/length
  span to read added. tile_thumb render thread never does vault I/O; read failures memoized
  like decode failures.

- **`ui::CoverCache`** — per-listing gallery cover spans, computed once and keyed by
  `IndexNode*`. Hosts (`GalleryGrid::refresh`, `SearchResultView::update_results`, import
  drains) invalidate on refetch. Grid refresh also clears `thumbs_.failed`.

- **`ui::remap_listing`** — preserves gallery scroll and multi-selection across drains by
  name. Import dicts remap selection by name and feed `child_names_` — the single fill
  site for `GalleryGrid::child_names_`. Grid can now detect re-exports and no-ops instead
  of always re-rendering.

- **SearchOverlay gather/filter split** — `gather_results` (vault walk, on open/scope-change/
  `on_vault_changed`) cached as hits; `filter_results` runs per keystroke over the cache.
  Public `on_vault_changed()` fixes dangling `SearchHit::node` pointers during drains.
  `GalleryGrid::on_vault_changed` notifies it.

- **`ui::Debounce`** — header-only timer. `AdvancedSearchScreen` debounces rerun to 150 ms
  input silence; flush before result-open and saved-search save; all other rerun sites
  immediate (simple/search overlay). Reduces per-keystroke latency on large result sets.

- **`ui::compact_album` + collection re-resolution** — `ImageViewer` collection-mode binds
  by path, re-resolves the album `IndexNode*` on `on_vault_changed`. Favorites and tag
  viewers do the same, eliminating dangling pointers and stopping video playback reset
  during imports.

- **`App::update(dt)` extracted from `run()`** — behavior identical; hosts the
  `app.update` perf scope. Render loop is now `frame()` (rendering) + `update()` (logic).

- **`platform::PerfScope`** — RAII tracing (`src/platform/perf.{h,cpp}`) with label +
  threshold (default 20 ms). `OSV_PERF_LOG=1` env flag enables logging. Fixed labels:
  `frame(20ms)`, `app.update`, `grid.refresh`, `grid.detail`, `search.gather`,
  `advsearch.rerun`, `thumb.fetch(5ms)`, `viewer.vault_changed`. Rebind fallback trace
  gated on perf_log_enabled, no vault-derived strings.

**No `INDEX_VERSION` bump** — no format changes.

**Out of scope:** full perf audit (hardening will follow); tile texture memory budget
(unchanged by design); async cover-span fetch (premature; bench first).

### Acceptance criterion

On a 200 GB vault with 50k+ items: tile grid renders at ~60 fps (no 0.5–1 s hangs),
thumbnail fetch is logged as 5–20 ms intervals (not per-frame I/O), scroll and playback
survive batch import. **1702 tests / 0 failed; `scripts/test.sh --asan` and `--tsan` clean;
`--no-av` parity at 1539 tests. `OSV_PERF_LOG=1 build/bin/Debug/osv` traces render frames,
updates, and vault ops.**

### Follow-ups

- **ROADMAP + detail doc:** Phase 58 has shipped; later hardening phases will tackle the
  remaining 20 % (decode-dimension cap, core-dump gating on Release, allocation guards).
  Perf audit post-stabilisation will identify any remaining render-thread IO.
- **`--no-av` fixture gap:** two tests failing without vendored AV have been marked as
  skipped / gated; see Phase 57 follow-up precedent.

**Status:** ✅ Shipped.
