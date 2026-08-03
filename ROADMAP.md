# Obscura-Safe-Vault — ROADMAP

> **Legend**
> - ✅ Done   🔜 In progress / planned   ⬜ Not started
> - Each phase ends with a clear acceptance criterion that must pass before work on the next phase begins.
> - Full spec for each phase (goal, tasks, acceptance criterion, status, follow-ups) lives in its own file under `docs/roadmap/`. This index is a scannable summary — open the linked file when working on that phase.

---

## Phase index

| # | Name | Status | Summary |
|---|---|---|---|
| 0 | Skeleton & minimal window | ✅ | Project structure, build system, a compilable app that opens a window. → [details](docs/roadmap/phase-00-skeleton.md) |
| 1 | Crypto core | ✅ | Full cryptographic primitive layer, implemented and tested. → [details](docs/roadmap/phase-01-crypto-core.md) |
| 2 | Vault container | ✅ | The `.osv` file format: header, index tree, chunk store, core vault API. → [details](docs/roadmap/phase-02-vault-container.md) |
| 3 | Image decode & thumbnails | ✅ | Decode images from decrypted memory buffers, generate encrypted thumbnails. → [details](docs/roadmap/phase-03-image-decode-thumbnails.md) |
| 4 | Graphics layer | ✅ | GPU texture cache and text atlas for the UI. → [details](docs/roadmap/phase-04-graphics-layer.md) |
| 5 | Unlock screen & gallery grid | ✅ | Vault layer connected to the UI — create/open/unlock a vault, browse galleries. → [details](docs/roadmap/phase-05-unlock-screen-gallery-grid.md) |
| 6 | Image viewer | ✅ | Full-screen image viewing with zoom/pan and the auto-scrolling thumbnail strip. → [details](docs/roadmap/phase-06-image-viewer.md) |
| 7 | Hardening & polish | ✅ | Close security gaps, handle edge cases, add deletion + compaction. → [details](docs/roadmap/phase-07-hardening-polish.md) |
| 8 | Cross-platform ports | ✅ | Windows and (later-removed) macOS build configs + CI pipeline. → [details](docs/roadmap/phase-08-cross-platform-ports.md) |
| 9 | Extra image formats | ✅ | WebP and HEIC/AVIF support. → [details](docs/roadmap/phase-09-extra-image-formats.md) |
| 10 | Export (selective, hard-gated) | ✅ | Deliberately extract decrypted images out of the vault, consent-gated. → [details](docs/roadmap/phase-10-export.md) |
| 11 | Slideshow | ✅ | Auto-advancing full-screen viewing of a leaf gallery. → [details](docs/roadmap/phase-11-slideshow.md) |
| 12 | Tags & Search | ✅ | Per-node tags on images and galleries, with cascade + live search. → [details](docs/roadmap/phase-12-tags-search.md) |
| 13 | Favorites | ✅ | Mark images/galleries as favorite, browse via two dedicated screens. → [details](docs/roadmap/phase-13-favorites.md) |
| 14 | Multiple vaults | ✅ | Manage and open several vaults; move images between them. → [details](docs/roadmap/phase-14-multiple-vaults.md) |
| 15 | Video playback (frames + seek) | ✅ | Store videos in the vault, play the video track, seek. → [details](docs/roadmap/phase-15-video-playback.md) |
| 16 | Audio & A/V sync | ✅ | Audio track added to the video pipeline with proper sync. → [details](docs/roadmap/phase-16-audio-av-sync.md) |
| 17 | Import ZIP archives | ✅ | Import a `.zip` of photos/videos into the vault in one action. → [details](docs/roadmap/phase-17-import-zip-archives.md) |
| 18 | Advanced search (dedicated screen) | ✅ | Weighted include/exclude + AND/OR groups + saved searches. → [details](docs/roadmap/phase-18-advanced-search.md) |
| 19 | Gallery cover thumbnails | ✅ | Representative cover art instead of the generic folder icon. → [details](docs/roadmap/phase-19-gallery-cover-thumbnails.md) |
| 20 | Advanced-search list/grid result views | ✅ | Toggle the advanced-search result panel between list and grid. → [details](docs/roadmap/phase-20-advanced-search-result-views.md) |
| 21 | Import a tag list onto a gallery | ✅ | Bulk-add tags to a gallery from a plain-text file. → [details](docs/roadmap/phase-21-import-tag-list.md) |
| 22 | Tag overview screen | ✅ | Dedicated screen listing every distinct tag with counts. → [details](docs/roadmap/phase-22-tag-overview-screen.md) |
| 23 | UI colour schemes | ✅ | Several selectable, runtime-switchable UI themes. → [details](docs/roadmap/phase-23-ui-colour-schemes.md) |
| 24 | Import CBZ archives | ✅ | Import a `.cbz` comic archive as a single page gallery. → [details](docs/roadmap/phase-24-import-cbz-archives.md) |
| 25 | Bugfixes & housekeeping | ✅ | Layout-independent keybindings, unified background-job progress UX. → [details](docs/roadmap/phase-25-bugfixes-housekeeping.md) |
| 26 | Transparent vault compression | ✅ | Adaptive store-if-smaller deflate framing before encryption. → [details](docs/roadmap/phase-26-vault-compression.md) |
| 27 | `meta.json` metadata on archive import | ✅ | Archive `meta.json` seeds the imported gallery's title + tags. → [details](docs/roadmap/phase-27-meta-json-metadata.md) |
| 28 | Broaden `.mov` / video codec support | ✅ | Decode ProRes/DNxHD/MJPEG beyond H.264/H.265. → [details](docs/roadmap/phase-28-mov-codec-support.md) |
| 29 | Tag autosuggest in the tag editor | ✅ | Autocomplete dropdown while typing a tag. → [details](docs/roadmap/phase-29-tag-autosuggest.md) |
| 30 | Import PDF as a gallery of pages | 🔜 | Import a `.pdf` as a gallery of rendered page images, like CBZ. → [details](docs/roadmap/phase-30-import-pdf.md) |
| 31 | Fullscreen viewing + edge-click navigation | ✅ | Borderless fullscreen + click-to-advance in the viewer. → [details](docs/roadmap/phase-31-fullscreen-viewing.md) |
| 32 | Background multi-file import | ✅ | Multi-select file-picker import runs on a background job. → [details](docs/roadmap/phase-32-background-multi-import.md) |
| 33 | Keep a vault unlocked for the session | ✅ | Session-only opt-out of the idle auto-lock timer. → [details](docs/roadmap/phase-33-keep-unlocked-session.md) |
| 34 | Import 7z, RAR, and TAR archives | ✅ | Extends ZIP/CBZ import to `.7z`/`.rar`/`.tar` via libarchive. → [details](docs/roadmap/phase-34-import-7z-rar-tar.md) |
| 35 | Password-protected archive import (ZIP/CBZ) | ✅ | Import a password-protected `.zip`/`.cbz`. → [details](docs/roadmap/phase-35-password-protected-archives.md) |
| 36 | Robust special-character filename & archive-name handling | ✅ | Safe node-name rules + legacy CP437 zip entry-name decoding. → [details](docs/roadmap/phase-36-special-character-filenames.md) |
| 37 | Persisted per-gallery sort order | ✅ | Choose + persist a gallery's sort order, including natural name order. → [details](docs/roadmap/phase-37-persisted-sort-order.md) |
| 38 | WebM video support (VP8/VP9) | ✅ | Import and play `.webm` video (Matroska + VP8/VP9). → [details](docs/roadmap/phase-38-webm-video-support.md) |
| 39 | Discoverable shortcuts & session-scoped gallery memory | ✅ | `F1` help popup + session-scoped gallery/viewer state memory. → [details](docs/roadmap/phase-39-discoverable-shortcuts-session-memory.md) |
| 40 | Video codec/loop/sync polish, gallery position memory & view density | ✅ | Part 1 ✅: AV1 + broader `.mov` codecs, video loop toggle, A/V sync hardening, + bugfix ✅: self-healing metadata repair for videos imported before their codec was decodable. Part 2 ✅: session-scoped gallery position memory (descend/ascend/leave-and-return restores the last-selected tile at every level). Part 3 ✅: 5-way List/Grid S-XL view density. → [details](docs/roadmap/phase-40-video-gallery-browsing-polish.md) |
| 41 | Async video decode | ✅ | Move CPU-heavy video codec decode off the render thread onto a background worker, so slow codecs (AV1/HEVC) don't stall playback/input/A-V sync. → [details](docs/roadmap/phase-41-async-video-decode.md) |
| 42 | ThreadSanitizer CI leg | ✅ | New `--tsan` build option + `tests-tsan` CI job, running the full suite under ThreadSanitizer on every PR to directly validate Phase 41's concurrent code (and any future threading) — reuses the plain vendored codec/SDL3 build rather than a parallel sanitizer-instrumented prefix. → [details](docs/roadmap/phase-42-tsan-ci.md) |
| 43 | Platform hardware-accelerated video decode | ✅ | Part 1 ✅: shared `media::HwAccelContext` infra + Windows D3D11VA, software `VideoDecodeWorker` as the automatic fallback. Part 2 ✅: VAAPI dlopen shim (`vendor/vaapi-shim`) + Linux enablement (`vendor/libva`, headers-only). → [details](docs/roadmap/phase-43-hardware-video-decode.md) |
| 44 | Gallery organization tools | ✅ | Part 1: scrollable + filterable Move-dialog gallery picker. Part 2: rename images/videos/galleries. Part 3: mass-move extended to galleries. Part 4: combine (recursive merge) two galleries, same- or cross-vault. → [details](docs/roadmap/phase-44-gallery-organization.md) |
| 45 | Organization, security & fullscreen polish | ✅ | Rename extended to favorites/tag-overview/search-result screens, mass tag add/remove, clipboard copy for password/passphrase, fullscreen hides the thumbnail strip, bigger video seek-bar hit target, auto-lock-off badge fades after 10s. → [details](docs/roadmap/phase-45-organization-ux-polish.md) |
| 46 | Mixed galleries (images + videos + sub-galleries together) | ✅ | Relax the leaf-only invariant so a gallery can hold any combination of media and sub-galleries, like a real folder. → [details](docs/roadmap/phase-46-mixed-galleries.md) |
| 47 | Animated GIF support | ✅ | Animated GIFs animate in the viewer (Space pauses) and on the hovered grid/strip tile, and carry an "A" badge. FFmpeg gif decoder + a new `animated` index flag (`INDEX_VERSION` 7). → [details](docs/roadmap/phase-47-animated-gifs.md) |
| 48 | Gallery detail panel | ✅ | A toggleable right-edge panel (`D`, `Ctrl+D` on advanced search) showing the focused node's metadata — type/codec, dimensions, size, date, own + inherited tags — plus a recursive tally for galleries and an aggregate summary for multi-selections. → [details](docs/roadmap/phase-48-detail-panel.md) |
| 49 | Colour-coded tag chips & per-vault settings | ✅ | Tags render as a coloured dot + bare name instead of `category:name`, on every tag surface. New global `F2` settings overlay (sidebar + pane) configures the per-vault category→colour mapping and a vault-wide default sort order; theme moves in from the deleted `C` picker. New vault-global settings block + explicit `Insertion` sort key (`INDEX_VERSION` 8). → [details](docs/roadmap/phase-49-tag-chips-settings.md) |
| 50 | Background import queue | ✅ | All imports (picked files, zip/cbz, 7z/rar/tar) run on a queueable background pipeline while the vault stays fully browsable — main-thread-only index tree, second read-only file handle, thread-safe chunk staging, whole-chunk write-mutex holds, CommitLane with stop-aware coalescing async batched index commits (~32 files / 2 s), parallel thumbnail-decode pool (min(hw,4)), strict in-order resequencing, lookahead cap 8 items/256MiB. New Import Status screen (`Shift+I`: progress, reorder, cancel, lane-failure banner) + live footer summary; idle auto-lock suppressed while queue non-empty, confirm dialog on manual lock/quit with password-at-enqueue for encrypted archives. → [details](docs/roadmap/phase-50-background-import-queue.md) |
| 51 | Tag metadata, folder import & organisation polish | ✅ | Per-tag free-text descriptions shown and edited in the tag overview (new vault-global block, `INDEX_VERSION` 9); tags roll up from a gallery's contents as well as down from its ancestors (read-time, nothing stored); import a folder as a gallery with subfolders mirrored as sub-galleries (`O`, reusing the ZIP tree planner); multi-select for archive and folder picks; direct-child counts on sub-gallery tiles; and an `F1` help popup that stops clipping lines and reflows into two columns. 1229 tests / 0 failed. → [details](docs/roadmap/phase-51-tag-metadata-folder-import.md) |
| 52 | Legacy container & codec support | ✅ | Play the video that dominated 2000–2010: add the AVI, MPEG-PS, MPEG-TS, ASF/WMV, FLV, Ogg and RealMedia demuxers plus MPEG-1/2, MPEG-4 ASP (DivX/Xvid), MS-MPEG4 v1–v3, WMV1/2/3, VC-1, H.263, Sorenson, DV, RealVideo and the lossless/legacy long tail. Adds yadif deinterlacing and honours `sample_aspect_ratio` so anamorphic DVD rips stop rendering squashed. **Note:** raw MPEG-PS (`.mpg`/`.mpeg`) is not decodable in the decode-only build (stores as Unknown-codec); MPEG-1/2 work via MKV/TS/MP4/MOV. Hardware decode (VAAPI/D3D11VA) was a compiled no-op from Phase 43; now fully functional. 1298 tests / 0 failed. → [details](docs/roadmap/phase-52-legacy-video-codecs.md) |
| 53 | Recursive & multipart archive import, gallery select-all | ✅ | Archives nested inside archives extract recursively, each becoming its own sub-gallery (depth cap 16, plus expanded-bytes / live-bytes / count / expansion-ratio guards against zip bombs); multipart sets import as one archive — `.7z.001`/`.zip.001`/`.tar.001` by concatenation, `.partN.rar`/`.r00` via libarchive's file-oriented API, and `.z01`/`.z02` via a new spanned-ZIP merger that rewrites central-directory disk numbers and offsets (libarchive and miniz both refuse multi-disk zips). Volume sets are auto-discovered and confirmed in a dialog that blocks on gaps. `Ctrl+A` toggles select-all over a gallery's direct children. **Known limits:** an encrypted split set fails rather than prompting (encryption is not probeable from one volume), and a multi-volume RAR imports flat. 1451 tests / 0 failed. → [details](docs/roadmap/phase-53-recursive-multipart-archives.md) |
| 54 | Editable input fields (caret, selection, clipboard) | ✅ | Every text field in the app gains a real caret: arrow-key movement, `Ctrl+Left/Right` word jumps, `Home`/`End`, `Shift`-extended selection, `Ctrl+A/C/X/V` and `Shift+Insert`, plus UTF-8-correct backspace (today every field deletes a *byte*). New pure SDL-free `TextInputModel` + `text_field_view` layout, and a `SecureBytes`-backed `SecureTextInput` that retires `SecureTextField` — paste into password fields is allowed, copy/cut out of them is not. All 18 fields (4 secure, 14 ordinary) migrated in one pass; `SecureTextField` is gone. Both backends share one `TextInputBase`, so their caret/selection semantics cannot drift, and one conformance suite runs against both. Two pre-existing defects fall out of the migration: the saved-search name prompt had no Backspace handler at all, and the tag-overview filter could never receive text (its browse-mode gate could not become true) — `/` now opens an explicit filter mode. Known limit: the font atlas bakes ASCII 32–126 only, so non-ASCII stores and pastes correctly but does not render. 1506 tests / 0 failed. → [details](docs/roadmap/phase-54-editable-input-fields.md) |
| 55 | JSON tag dictionary import | ✅ | Import a JSON file of `{category, name, description}` entries to populate the vault's tag *vocabulary* — registering categories with auto-assigned swatches (lowest free palette index, wrapping round-robin past 16) and storing per-tag descriptions in the existing Phase 49/51 vault-global blocks. It tags no images. `Ctrl+I` on the tag overview, exception-free nlohmann parsing per the Phase 27 pattern, one settings commit at the end. Two new pure modules — `tag_json_parse` (bare array or `{"tags":[…]}`, trims every field, rejects a colon in `name`, truncates category/description on a UTF-8 boundary, case-insensitive de-dupe) and `tag_dict_import` (over `VaultSettings`, so the whole import unit-tests without opening a vault). An empty `description` **preserves** any existing one — a deliberate divergence from Phase 51's edit-time "empty removes" rule. No cap is silent: the summary modal reports categories/descriptions added, descriptions updated, entries skipped (malformed / vault full), categories not registered, and fields shortened. **Known limit:** the overview lists tags something in the vault carries, so a description imported for an unused tag is stored but has no row until that tag is used. **No `INDEX_VERSION` bump** — writes only into blocks that already exist. 1556 tests / 0 failed. → [details](docs/roadmap/phase-55-json-tag-dictionary-import.md) |
| 56 | UI layout, import status & navigation polish | ✅ | Six UI defects. One font-derived text pitch (`ui::line_pitch`, `ceil(font_px * 1.25)`) replaces every hardcoded line height, so a scrolled popup can no longer cut its bottom line (the 28 px font was being laid out on 20–25.5 px pitches). Import Status rows become two lines — `source → target` above, progress bar or outcome below — so the bar stops overlapping the text and finished items keep showing where they came from and went; selection becomes id-based so `Ctrl+Up`/`Ctrl+Down` keeps focus on the item it just moved. The viewer re-binds by path instead of refitting on `on_vault_changed`, so a background import's commit no longer resets zoom/pan/scroll or restarts video playback. Right-click becomes a universal "back / up one level" by translating to a synthetic Esc at the single event funnel. Plus a HiDPI fix found while tracing: layout is in render pixels but SDL delivers mouse events in window points, so every hit-test was off by the display scale on Windows. 1636 tests / 0 failed. → [details](docs/roadmap/phase-56-ui-layout-import-status-navigation.md) |
| 57 | Animated WebP support | ✅ | Animated WebP files get the full Phase 47 GIF treatment: the "A" badge in the grid and thumbnail strip, animation in the viewer with `Space` to pause, and 200 ms hover auto-play under the same 1920×1080 / 300-frame budget. Also a bug fix — animated WebPs cannot be imported **at all** today (`WebPGetInfo` returns the `VP8X` canvas size so the size check passes, then `WebPDecodeRGBInto` fails on the `ANIM`/`ANMF` payload), which also means no existing vault can contain one. Decoding uses libwebp's own `WebPAnimDecoder` (`libwebpdemux.a` is already built and vendored), so unlike GIF it needs **no FFmpeg** and works in every build. Phase 47's format-neutral machinery is generalized rather than duplicated: `gif_playback`/`gif_model`/`gif_repair`/`gif_info` become `anim_*` over a new `media::AnimDecoder` interface with `GifDecoder` and a new `WebpAnimDecoder` behind it. Transparency is flattened over black (matching how the app already treats transparent PNGs/GIFs) and the file's loop count is ignored, so playback matches GIF exactly. **No `INDEX_VERSION` bump** — `ImageMeta::animated` was already format-neutral. One new predicate, `vault::format_can_animate`, is the single source of truth for import, repair, the sniff gate, the badge and the hover gate. Fixed a pre-existing break found while restructuring: `gif_playback.cpp` opened `namespace ui` *inside* its `#ifdef OSV_VENDORED_AV`, so that translation unit did not compile at all without vendored FFmpeg — Phase 47's non-FFmpeg fallback was never buildable, and no CI job would have caught it. 1677 tests / 0 failed. → [details](docs/roadmap/phase-57-animated-webp.md) |
| 58 | Big-vault responsiveness & import-time UI stability | ✅ | Render thread no longer performs synchronous vault I/O for thumbnails: `DecodeWorker::submit_fetch` adds a worker-thread fetch stage (vault-agnostic `Fetcher` callable), `Vault` gained a dedicated async-safe background `thumb_fp_` handle under mutex, and read failures are memoized to prevent retry loops. UI refresh no longer re-resolves cover spans per frame — `CoverCache` computes them once per listing, invalidated on gallery refetch. Search overlay separated into gather-once (vault walk) and filter-per-keystroke (predicate over cached hits), debounced to 150 ms input silence on advanced search only. Gallery refresh during batch import preserves scroll via `remap_listing` — selection re-keyed by name, not tile identity. Collection-mode viewers (favorites, tag overview, search results) re-resolve their album `IndexNode*` on vault change, fixing dangling pointers. App::update extracted from run() to host perf tracing. Instrumentation added: `platform::PerfScope` RAII tracing (`OSV_PERF_LOG=1` env flag) with fixed labels for 20 ms frames, app.update, grid refresh/detail, search gather, advanced-search rerun, 5 ms thumbnail fetch, and vault-rebind fallback. **Root causes fixed:** (a) uniform 0.5–1 s UI latency on 200 GB / 50k+ item vaults — render thread doing per-frame seek+read+decrypt per missed thumbnail, per-frame cover re-resolution and failed-read retry, search per-keystroke vault walk; (b) video playback/scroll reset during imports — grid `on_vault_changed` cleared selection state, collection viewers held dangling node pointers. 1702 tests / 0 failed. → [details](docs/roadmap/phase-58-big-vault-responsiveness.md) |
| 59 | Non-blocking video seek | ✅ | Seeking (J/L, frame-step back, seek-bar click/drag, loop-at-EOF, session resume) no longer freezes the render thread: `do_seek()` used to run the seek's decode-forward — from the anchor keyframe to the target — to completion inside the blocking `decode_into_pending()`, which on long-GOP / slow-software-codec clips (AV1, HEVC) meant seconds of unpumped events and a compositor "not responding" flag; scrubbing fired that per mouse-motion event. Now `do_seek()` is a pure state transition (demux reseek, worker `begin_seek`, generation bump, audio re-base, transport jump) that returns immediately; the previously shown frame stays up until the target frame lands. The existing `skip_pending_` flag drives the rest: `animating()` holds true while a seek is resolving so `App::run` keeps ticking even when paused, `try_advance_pending()` tops the worker up to a deeper `SEEK_FEED_DEPTH` (32) and feeds uncapped on timeout while it is set, and `consume_result()` realigns the transport to the decoded frame's actual pts on resolve. Only the constructor's frame 0 still uses the blocking path (pts 0 is a keyframe — no gap). No new threads, no new shared state, no format change. 1714 tests / 0 failed. → [details](docs/roadmap/phase-59-async-video-seek.md) |
| 60 | In-place vault compaction (dead-space packing) | ✅ | `Vault::compact()` now packs live chunks into dead space in place — O(1) extra disk instead of the old ~2x copy-rewrite, so a 200 GB vault no longer needs another 200 GB free to compact. Crash-safe by construction: moves land in dead space per the last-committed index; batch commits use the existing 3-phase slot-swap with no journal or recovery path. Cancellable and resumable: Esc keeps completed work; re-running continues. Final index blob is placed just after the packed data, dead tail is truncated, residual holes punched (Linux). Copy-rewrite machinery deleted; Windows auto-reclaim no longer transiently doubles disk use. 1724 tests / 0 failed. → [details](docs/roadmap/phase-60-in-place-compact.md) |
| 61 | Duplicate finder | ✅ | `Ctrl+D` on the gallery grid opens a whole-vault duplicate scan — exclusive op behind the same import-queue gate as compact, with a new "Vault tools" F1 group. The screen opens in a chooser state: **exact** (group by type+size from a main-thread snapshot, then BLAKE2b over the full plaintext decrypted into mlock'd `SecureBytes` and wiped immediately — only size-colliding files are ever decrypted) or **exact + visually similar** (64-bit dHash over the stored thumbnails, union-find clustered at Hamming ≤ 5, images only). The background `DupScanJob` only ever calls the thread-safe `vault::read_thumb_span` over the snapshot — the main-thread-only index tree is never touched off-thread; bad files are skipped and counted, manual lock cancels the worker before key wipe, and hashes are session-lifetime heap data (never persisted, never logged — `INDEX_VERSION` stays 9). Review lists groups largest-reclaimable-first as side-by-side tiles (name, parent path, size, resolution) with KEEP/REMOVE badges: `Space` toggles, `A` keeps only the focused member, `Enter` full-screen inspects the decoded original, and a pure-model invariant blocks apply while any group is all-REMOVE (that would be deletion, not de-duplication). `Ctrl+Enter` → default-cancel confirm → new vault free friend `vault::remove_media_batch` — N erases, ONE `commit_index()` + one `auto_reclaim_space()`, i.e. one crash-safe slot swap instead of one fsync per file. 1757 tests / 0 failed. → [details](docs/roadmap/phase-61-duplicate-finder.md) |
| 62 | Perceptual video duplicates | ✅ | Phase 61's "Exact + visually similar" scan now catches re-encoded / resized / remuxed copies of the same video (before, only byte-identical videos grouped). Three-stage funnel, cheapest evidence first: duration gate from `vmeta.duration_us` (±2 %, 500 ms floor — zero I/O), poster dHash prefilter (stored first-frame JPEG, Hamming ≤ 10), then a frame confirm that software-decodes 5 frames at 10–90 % of the timeline and requires ≥ 4 of 5 per-position dHashes within Hamming ≤ 7. Frame decode runs through a new `ui::compute_video_frame_sig` over a callback AVIO backed by a chunk-caching `vault::read_thumb_span` byte stream — the worker never touches the main-thread `read_fp_`/`VideoSource`. Union-find clusters matching pairs into "Similar video" groups; review/marking/apply unchanged; non-FFmpeg builds accept the duration+poster verdict (stub). No new UI mode, no `.osv` change. Fixtures: the same 2 s `testsrc2` clip pre-encoded as H.264/MP4 **and** VP9/WebM (decode-only FFmpeg cannot re-encode at test time; lavfi `gradients` was rejected — it randomizes colors per invocation). Found & fixed in the decode loop: after the demux-EOF flush, `av_packet_unref` resets `stream_index` to 0, and sending that stale packet errored out late timeline positions on single-keyframe files. 1774 tests / 0 failed. → [details](docs/roadmap/phase-62-perceptual-video-duplicates.md) |
| 63 | Vault growth on unlock (Windows) & duplicate-review UX | ✅ | Two owner-reported defects. **(a) Old vault grows + lags on every unlock:** `repair_video_metadata()` appended the re-probed poster chunk on `fp_` *without* `write_mutex_`, racing the CommitLane worker (started at unlock) committing the previous repair's index blob on the same `FILE*` — the interleaved seek+write could misplace `write_header`'s payload at EOF (the PR #109 failure class), losing the active-slot flip, so the next unlock loaded the stale slot, saw the videos as Unknown again, and re-ran the repairs — appending new posters + index blobs every session. Windows widens the race (slow `FlushFileBuffers`) and never hole-punches the dead bytes. Fixed by holding `write_mutex_` across the append; the silent slot-fallback in `unlock()` now logs; and a transient `VideoMeta::probe_failed_session` memo stops gallery refresh re-reading whole undecodable videos on every visit. Regression-tested by repairing 8 forced-Unknown videos under a live CommitLane and cold-reopening. **(b) Duplicate review:** groups now default to KEEP on the first member with the rest pre-marked REMOVE (an untouched review applies straight to one-copy-per-group); `Space` can no longer unmark a group's last keeper (all-REMOVE unreachable, refusal shown in the footer); leave-confirm + idle-lock-block gate on user-touched marks; and the full-screen inspect texture is now owned by the screen instead of passing through the shared thumbnail cache, whose eviction was blanking the preview (and the tiles) to black — the `FullTexCache` rationale applied to inspect. 1787 tests / 0 failed. → [details](docs/roadmap/phase-63-unlock-growth-dup-review-ux.md) |
| 64 | Wave-based duplicate review & review-screen memory fix | ✅ | Two independent fixes. **(a) Review-screen memory churn:** `render_review` drew every group row each frame with no culling, and submitted decode fetches for every member; once the thumbnail set exceeded the 256 MiB `TextureCache` LRU the screen churned indefinitely — decrypt → decode → evict → refetch thousands of times per pass. Fixed: cull off-screen rows, only visible tiles fetch, and `DecodeWorker::retain(visible ∪ inspect)` each frame to prune stale requests. RAM now bounded by visible tiles independent of group count. **(b) Wave UX:** review presented in 20-group waves resolved incrementally — `Ctrl+Enter` applies the current wave (one `remove_media_batch` + accumulate totals), `N` skips a wave with confirm. Post-apply span re-resolution re-reads all surviving members by path (Windows `compact()` relocates chunks) — `failed_` memo cleared, groups shrinking below 2 members dropped. Header shows wave N/M, this-wave groups, bytes reclaimable, total remaining. No `.osv` change, no `INDEX_VERSION` bump. 1799 tests / 0 failed. → [details](docs/roadmap/phase-64-dup-wave-review.md) |

---

## Container format spec (reference)

Reproduced from `mem:vault_format` (Serena) for quick access during vault
implementation work.

```
Offset  Size  Description
──────  ────  ───────────────────────────────────────────────────────────────
0       8     magic: "OSVAULT\0"
8       2     version (u16, currently 1)
10      2     header_size (u16, total header length in bytes)
12      4     flags (u32, reserved)

16      1     kdf_algo  (0 = Argon2id)
17      4     t_cost    (u32, Argon2id time cost)
21      4     m_cost_kib (u32, Argon2id memory cost in KiB)
25      4     parallelism (u32)
29      16    salt      (u8[16], random)
45      1     keyfile_required (u8, 0 or 1)

46      24    master_key_nonce (u8[24])
70      32    wrapped_master_key (u8[32], XChaCha20-Poly1305 ciphertext)
102     16    master_key_tag (u8[16], Poly1305 tag)

118     8     slot_a_offset (u64)
126     8     slot_a_length (u64)
134     24    slot_a_nonce  (u8[24])
166     8     slot_b_offset (u64)
174     8     slot_b_length (u64)
182     24    slot_b_nonce  (u8[24])
206     1     active_slot   (u8, 0 = A, 1 = B)

207     N     reserved padding (zeroes, up to header_size)
```

**Data region** starts at `header_size`. Each encrypted chunk is laid out as:
```
  nonce[24] | ciphertext[plaintext_len] | tag[16]
```

**Framed vaults (header flags bit 0, Phase 26):** the AEAD plaintext of every
chunk AND of the sealed index blob is a chunk_codec frame:
```
  method u8 (0 = raw, 1 = deflate)
  if raw:     payload bytes
  if deflate: orig_len u64 LE | zlib-wrapped deflate stream
```
Flag clear (legacy): the plaintext is the payload verbatim, read and appended that
way forever.

**Index blob** (binary serialised; `INDEX_VERSION = 6`):
```
  version    u8
  root       node              (the tree, recursive — see below)
  saved_searches (v5+):                (Phase 18; omitted in v1–v4 blobs → empty)
    count    u16
    entries  { name_len u16; name u8[name_len];
               query_len u32; query u8[query_len] } [count]
```

**Index tree node** (binary serialised):
```
  node_type  u8  (0 = gallery, 1 = image, 2 = video)
  name_len   u16
  name_len   u16
  name       u8[name_len]  (UTF-8)

  tag_count  u16                     (Phase 12; v2+. Omitted entirely in v1 blobs.)
  tags       { tag_len u16; tag u8[tag_len] (UTF-8) } [tag_count]

  favorite   u8                      (Phase 13; v3+. Omitted in v1/v2 blobs → 0.)

  if gallery:
    sort_key     u8                 (Phase 37; v6+. Manual/NameAsc/NameDesc/
                                      DateAsc/DateDesc/SizeAsc/SizeDesc.
                                      Omitted in v1–v5 blobs → Manual.)
    child_count  u32
    children     node[child_count]  (recursive)

  if image:
    format       u8  (0=JPEG, 1=PNG, 2=GIF, 3=BMP, 4=TGA, 5=HDR, 6=WebP, 7=HEIC, 8=AVIF)
    width        u32
    height       u32
    orig_size    u64  (plaintext bytes)
    created_ts   u64  (Unix timestamp, seconds)
    data_offset  u64
    data_length  u64
    thumb_offset u64
    thumb_length u64
```

> **Format extensions (Phases 12–18).** The index serialisation is
> versioned; each of these bumps `INDEX_VERSION` and reads older versions with
> the new fields defaulted, so existing vaults keep opening cleanly:
> - **Phase 12 (Tags):** ✅ shipped — a tag list (`u16 count` + length-prefixed
>   UTF-8) on **both** gallery and image nodes, written after `name`
>   (`INDEX_VERSION = 2`; v1 blobs read with empty tags). Gallery tags cascade to
>   descendants at read time (effective tags = own ∪ ancestors'); they are not
>   copied onto children.
> - **Phase 13 (Favorites):** ✅ shipped — a `favorite u8` flag on both node types,
>   written after the tag block (`INDEX_VERSION = 3`; v1/v2 blobs read as
>   not-favorited).
> - **Phases 15–16 (Video):** a video node kind (a `media_kind` discriminator)
>   and new `format` codes appended after `8=AVIF` (e.g. `9=MP4/H.264`); video
>   nodes reuse the same `data_*`/`thumb_*` layout (thumb = poster frame), with
>   the container stored across multiple encrypted chunks.
> - **Phase 18 (Advanced search):** ✅ shipped — a **vault-global saved-searches
>   block** serialised after the tree root (`u16 count` + per-entry `{ name,
>   serialised query }`, the query an opaque `ui::AdvancedQuery` blob), bumping
>   `INDEX_VERSION` to **5**; pre-v5 blobs read with an empty saved-searches list.
>   The block is not part of any node — it is vault-level metadata, persisted via
>   the same crash-safe double-buffer index swap and bounded against the fuzz suite.
> - **Phase 37 (Persisted sort order):** ✅ shipped — a `sort_key u8` on every
>   Gallery node (`INDEX_VERSION = 6`; v1–v5 blobs read as `Manual` — no visible
>   change until a user opts in via `Shift+S`). An out-of-range byte is
>   rejected on deserialise, not clamped.
> - **Phase 47 (Animated GIFs):** ✅ shipped — an `animated u8` flag on every
>   Image node, written after `thumb_length` (`INDEX_VERSION = 7`; v1–v6 blobs
>   read as not animated and are healed lazily on first view). A byte other
>   than 0/1 is rejected on deserialise, not clamped.
> - **Phase 49 (Tag chips & per-vault settings):** ✅ shipped — a **vault-global
>   settings block** serialised after the saved-searches block (`default_sort u8`,
>   `tiles_show_tags u8`, then `u16 count` + per-entry `{ name, swatch u8 }` tag
>   categories), bumping `INDEX_VERSION` to **8**; pre-v8 blobs read with the
>   default sort `Insertion`, tile tags on, and the built-in category seed. The
>   same bump re-reads a Gallery's `sort_key` byte 0 as `Default` ("follow the
>   vault default") and adds `7 = Insertion` for raw import order, so existing
>   galleries adopt the vault default with no migration. Out-of-range
>   swatch/sort/flag bytes are rejected on deserialise, not clamped.
