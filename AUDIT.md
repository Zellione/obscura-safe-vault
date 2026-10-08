# Audit: phases 110–115

Audit date: 2026-10-08. Baseline: `d3dd346` (Phase 115), compared with
`6a484b3` (before Phase 110). Scope: directory-vault feature parity with the
classic single-file backend, conversion, integrity/lifecycle behavior, and
first-party remnants of removed Windows/macOS support.

Findings and remediation evidence are recorded incrementally below. Existing
untracked `.cache/` is out of scope.

## Remediation progress

Branch: `fix/audit-110-115`. Original findings below retain the audited baseline.

| Finding | Status | Validation |
|---|---|---|
| A09 | Implemented: directory commits use transactional backend | Queue empty shutdown/import/cold reopen regression passes |
| A01/A02/A03/A10 | Implemented: schema v2 preserves microseconds, case-distinct names, tag spelling/order; secure tag temporaries | Conversion, encrypted v1 upgrade (including immutable read-only copy), >4096 global tags and 1024-byte tag tests pass |
| A05/A06 | Implemented: explicit original/frame reads, immutable-object cache identities | Original/duplicate/frame reads, precommit reads, replacement keys, and actual MigrationJob tests pass |
| A07/A08 | Implemented: incomplete conversion gate and lease-protected header refresh | Original partial-conversion/stale-password probes pass |
| A04 | Implemented indexed node/role lookups | Debug facade: 1k/2k objects unlock 19.1 ms, 5k/10k objects unlock 92.6 ms; 2000 thumbnail reads 51.1/45.1 ms |
| A11/A12 | Implemented completeness verification and explicit damaged unlock result | Missing-original and missing-object unlock/diagnostic tests pass |
| A13 | Frame-loop cancellation implemented | Cancellation after a video frame and during deep verification passes; both resume successfully |
| Removed-platform inventory | Implemented | Removed dead console redirects/macOS CPU fallback/non-Linux reclaim branch; updated current docs/memories; naming restrictions retained |

### Current validation

- `scripts/test.sh`: **2391 tests, 0 failed**.
- Focused audit regressions: **18 tests, 0 failed**.
- `serena memories check`: no referential integrity issues.
- `scripts/test.sh --release`: **2390 tests, 0 failed**.
- Release facade benchmark: 50,000 media / 100,000 objects; batched import
  **111.4 s**, cold unlock **1.413 s**, 2,000 thumbnail reads **10.7 ms**.
- `scripts/test.sh --asan`: **2391 tests, 0 failed**, exit 0, no sanitizer findings
  (unrestricted run for LeakSanitizer thread inspection).
- No-FFmpeg Debug: **2196 tests, 0 failed**; standard build files restored.
- Final cancellation-token/secure rebuild-buffer sanitizer run: **2391 tests, 0 failed**, exit 0.
- Actual migration-job regressions: **22 tests, 0 failed**.
- Final Release/no-AV recheck and CI/SonarCloud: pending.
- Scale results above use synthetic small objects on this machine. They do not
  establish the former 250k-media responsiveness claim; the opt-in production
  benchmark now supports that measurement rather than substituting SQL timings.

## Findings (audited baseline)

Priority: A09 (application abort), A05 (false duplicate deletion), A06 (wrong
displayed media), then conversion blockers A01/A02/A10. Feature parity is **not
met**, despite the existing suite passing.

### A01 — High: conversion rejects valid sub-millisecond video durations

`src/vault/v3_db.cpp:115` stores `duration_us / 1000`, and
`src/vault/v3_read_session.cpp:78` reconstructs milliseconds times 1000.
`src/vault/legacy_converter.cpp` then requires exact `duration_us` equality in
`logical_metadata_equal`. Any legacy duration not divisible by 1000 loses
precision and fails final conversion verification. Existing conversion video
coverage uses `tiny.mp4`, which does not exercise this boundary. Preserve the
original precision in persistence; do not merely remove the verification check.
Confirmed by a focused executable probe: a valid prestaged video with duration
`1,234,567` microseconds returns `VerificationFailed` (status 10).

### A02 — High: tag persistence changes ordering and spelling during conversion

`Database::node_tags` (`src/vault/v3_db.cpp:1131`) returns canonical-name order;
legacy tags retain insertion order. `sync_metadata` also coalesces spelling
case-insensitively across the entire vault, taking the first display spelling.
The converter compares tag vectors exactly. Legitimate reversed-order tags or
different casing on different nodes can therefore prevent conversion. Settings
description/value vectors have similar SQL ordering versus exact-comparison
risks. Confirmed independently with `{"zebra", "alpha"}` on one image and
`{"Blue"}` / `{"blue"}` on two images: both return `VerificationFailed`.

### A03 — Medium: new database synchronization copies decrypted tags to ordinary heap

`src/vault/v3_db.cpp:709` builds `vector<pair<string,string>> tags`, plus
temporary ordinary `std::string canonical` values, from secure node tags and
settings. These application-owned plaintext copies are neither page-locked nor
wiped on destruction. This regresses the Phase 91 secure-metadata boundary;
SQLCipher's documented opaque-allocation limitation does not cover these
application-owned buffers. Use secure strings/containers for these temporaries.

### A04 — Medium: scale benchmarks bypass the actual production read path

`ReadSession::materialize` loads a complete tree and `apply_object_markers`
scans the full object-reference vector for every node. `object_for` scans both
references and the tree for every read. `commit_metadata` repeats the tree copy
and object-marker scan. At N media nodes and O referenced objects the marker
pass is O(N × O). The Phase 114 benchmark exercises the database directly;
its claim that unlock cost is dominated by KDF, and thumbnail scrolling is
independent of metadata cardinality, does not validate these production paths.
Measure facade unlock/import/thumbnail reads with representative object counts
and index reference lookups by node/role before claiming the large-vault gate.

### A05 — High: original reads route to thumbnails, producing false exact duplicates

`src/vault/vault.cpp:1271` (`read_thumb_span`) is historically the generic
thread-safe chunk reader despite its name. The new v3 branch maps only Poster
explicitly and maps **every other domain to Thumbnail**, including Data and
Video. `src/ui/dup_scan.cpp:106` hashes originals through this API;
`src/ui/migration_job.cpp:145` also reads original media through it.

Confirmed: requesting an original `[1,2,3,4]` returns its thumbnail
`[5,6,7,8]` with `Ok`. Two different equal-size originals sharing a thumbnail
produce one `Identical` duplicate group, with zero skipped items. Acting on
that result can delete unique originals. Images without thumbnails are
skipped; v3 video chunk references contain zero-length placeholder spans and
fail the early length guard, so exact video scanning/rebuilds fail too.
Image rebuilds can decode/recompress an existing thumbnail instead of the
original, or fail for missing thumbnails. Introduce a role-aware original/frame
read interface and run actual duplicate/migration jobs against both backends.

### A06 — High: all newly imported images share the same display cache identity

`src/vault/staging.cpp:103` assigns `data_offset = 1`, and thumbnails/posters
also receive offset 1. `ReadSession::commit_metadata` calculates node-derived
markers only in its private tree; `Vault::commit_index` does not copy those
markers back to the public facade tree. `ui::thumb_key_for`
(`src/ui/tile_thumb.cpp:21`) and `FullTexCache::acquire`
(`src/ui/full_tex_cache.cpp:32`) use these offsets as cache identities.

Confirmed: importing two distinct images and committing both returns cache
keys `1,1`. The UI can show another item's thumbnail/full-size texture or share
its failed-decode state until lock/reopen materializes distinct markers.
Derived-object replacement functions also assign offset 1. Assign stable,
distinct identities before attaching nodes and include replacement generation
where cache invalidation requires it; test the live session, not only reopen.

### A07 — Medium: an incomplete conversion opens as an ordinary writable vault

`ReadSession::unlock` never consults `conversion_marker`; normal open/promotion
can therefore bypass the converter's completion gate. Confirmed: cancel just
after destination creation, then open/unlock the destination through `Vault`;
unlock returns `Ok` and `vault_is_read_only` is false. A partially copied tree
can be mistaken for a completed vault. Worse, normal edits can later be
overwritten by conversion resume's source-tree synchronization. Normal opening
must surface an incomplete-conversion state and route to resume/recovery.

### A08 — Medium: locked sessions use stale password-wrap headers

`ReadSession::open` caches the header before acquiring the writer lease;
`unlock` and `change_password` use that cached header without rereading after
lease acquisition. Confirmed: open a second locked handle, rotate the password
through the first handle, release its lease, then unlock the second with the
old password: it returns `Ok`. No prior unlock of the second handle is needed.
Refresh and validate the on-disk header under the lease before authentication
or rewrap. This does not eliminate the accepted offline rollback limitation,
but prevents normal live sessions from accidentally using superseded wraps.

### A09 — Critical: the production import queue still uses the single-file commit lane

`src/app/app.cpp:244` unconditionally starts `CommitLane` for the active vault.
`ImportQueue::drain`, `maybe_end_batch`, and `abort_and_flush`
(`src/ui/import_queue.cpp:696`, `1330`, `402`) call `enqueue_snapshot` directly,
bypassing `Vault::commit_index`'s v3 dispatch. `CommitLane::worker_loop`
(`src/vault/commit_lane.cpp:157`) dereferences the legacy `write_mutex_` and
then writes through the legacy `FILE*`; directory creation/open initializes
neither. This affects normal import batching and final/idle queue flushes.

Confirmed in an isolated child process: create a directory vault, start the
same lane, enqueue a snapshot, flush. The Debug build aborts with signal 6 at
the null `unique_ptr<mutex>` dereference. Production must route queue commits
through a backend-aware durability interface; merely bypassing the lane in
`Vault::commit_index` is insufficient. Add v3 ImportQueue end-to-end tests,
including empty-session shutdown and imported-data cold reopen. Current queue
fixtures create classical vaults, so the green suite misses this integration.

### A10 — High: SQL name/tag constraints reject previously valid vault content

`src/vault/v3_schema.h:42` makes sibling names case-insensitively unique;
classic `child_named` uses exact spelling. Confirmed: a legacy vault containing
galleries `Trip` and `trip` creates and reopens successfully but conversion
returns `IoError` (status 8). The same schema rule affects media names.
The schema originates before phase 110, but phase 112/115 persistence and
conversion expose it as a compatibility regression.

Additional differences needing explicit compatibility handling:
`sync_metadata` caps **global distinct tags** at 4096 (`v3_db.cpp:729`), whereas
`INDEX_MAX_TAGS` bounds tags **per node** in classical vaults; SQL tags are
limited to 255 bytes while the legacy format allows a u16-length tag. Align
the accepted domain with the classic format, or provide an owner-approved,
non-destructive migration policy with actionable errors. Do not silently
rename/coalesce content or report an undifferentiated I/O failure.

### A11 — Medium: deep verification does not check that media has an original

`src/vault/v3_recovery.cpp:146` authenticates rows returned by
`object_references`; neither the schema's foreign keys nor verification
requires an image/video node to have its corresponding original-object row.
Confirmed through `ReadSession::commit_metadata`: persist an image node with
nonzero original size and no object; Deep verify returns `Ok`, zero checked
objects, zero findings. Thus a missing metadata reference is invisible to
verification even though the gallery item cannot be read, and backup can
propagate this logically incomplete state. Check media-role completeness,
node/role compatibility, and logical size agreement, with a deliberate policy
for optional derived objects and incomplete conversion states.

### A12 — Medium: quick-open corruption findings are discarded

`src/vault/v3_read_session.cpp:224` assigns the quick verification result to
`quick_open_report_`, then unconditionally sets `unlocked_ = true` and returns
`Ok`. Repository search finds no consumer/accessor for that report. Missing or
wrong-sized referenced objects therefore do not produce the promised unlock
diagnostic or degraded-state warning. Opening for recovery can be useful, but
it must expose the report and clearly distinguish a damaged session. This is
confirmed by control-flow review; no additional destructive fixture was needed.

### A13 — Medium: conversion cancellation has unbounded latency within large media

The only `cancelled(request)` checks in `legacy_converter.cpp` are at node
entry and `stage_media` entry. `LegacyVideoReader`, the streaming object write,
deep verification, cold-reopen verification, and original-digest comparison
do not receive/check cancellation. Escape or shutdown during one large video
must wait for that copy; cancellation during final verification can wait for
multiple whole-vault reads. `LegacyConversionJob` joins its worker on teardown,
so this is user-visible despite bounded video-buffer memory. Thread a cancel
token through frame loops and verification, and test cancellation mid-frame
sequence and during the final verification phase. Static finding; wall-clock
latency depends on vault size and storage throughput.

## Windows/macOS removal inventory

No active Win32/Cocoa implementation or Windows/macOS CI job was found in the
first-party production files reviewed. Most remnants are comments, current-state
documentation, no-op APIs, and conservative naming rules. Do not edit vendored
submodules to remove their upstream platform support.

| Candidate | Evidence and recommendation |
|---|---|
| Remove dead Windows console redirect plumbing | `src/platform/harden.cpp:29–40`: `redirect_diagnostics_to_log_file` is a Linux no-op; `redirect_stream_to_file` has only test callers. Remove declarations, the `app.cpp:78` no-op call, and the three redirect tests together. `freopen_path` then has no other callers in `src/` and can be removed as part of that change. |
| Remove macOS CPU-count fallback | `scripts/{setup,build,test,build_codecs}.sh` still use `sysctl -n hw.ncpu`. Linux can retain `nproc` plus a numeric fallback. |
| Remove unreachable non-Linux branches | `src/vault/file_util.h:77` has a non-Linux hole-punch stub; `Vault::auto_reclaim_space` has a non-Linux compact fallback. Simplify guards under the Linux-only build policy; retain runtime unsupported-filesystem handling. |
| Correct setup/build commentary | `scripts/setup.sh:36` directs users to deleted `setup.bat`; `scripts/build_codecs.sh:182` refers to deleted `build_ffmpeg_windows.sh`. `premake5.lua` still has explanatory Windows linkage comments. |
| Correct current-state memories | `.serena/memories/suggested_commands.md:85` lists `setup.bat`, VS/MSBuild, and `package.ps1`. `tech_stack.md:30,149–168` describes removed Windows library branching, CFG/CET, ccache/MSBuild jobs and two release packages. `core.md` still says legacy files are read-only “until” the already-completed converter. `module/vault.md` still describes `_fstat64`, `_chsize_s`, and Windows reclaim behavior. |
| Correct API/app comments | Stale VirtualLock/working-set explanations in `app.cpp` and `platform/harden.h`; Windows narrow-path/DACL/CREATE_NEW claims in `platform/paths.h`, `file_dialog.cpp`, `atomic_file.h`; Windows compaction rationale in `ui/dup_scan.h` and `duplicates_screen.cpp`. `AGENTS.md` still leads with a single-file goal and a generic non-crash-safe password-change warning despite v3's different implementation. |
| Optional naming-policy simplification, **not** a blind deletion | `vault/safe_name.cpp` retains DOS-device rejection, Windows-only forbidden punctuation, and trailing-dot/space trimming. Those restrictions are unnecessary for Linux internal opaque object names, but removing them changes accepted names, export behavior, and tests. Make an explicit policy decision; always retain NUL/control/path traversal checks and atomic export containment. |

Keep these even though their comments mention Windows/macOS:

- `platform::safe_println`: nonthrowing diagnostics remain useful on Linux with
  broken/closed streams or formatting allocation failures.
- `openat2` → `openat` fallback: needed for Linux kernels/seccomp policies,
  not removed-platform support. Keep descriptor-relative no-follow containment.
- Phase 80 swscale padding/canary tests: the overrun also occurs on Linux.
- CP437/archive-name handling, WMV decoding, and historical format fixtures:
  input compatibility is independent of the host OS.
- Legacy reader, converter, and test-only writer: required for migration and
  compatibility/crash fixtures; do not confuse single-file support with OS support.
- `_WIN32` / `__APPLE__` compile-time rejection guards: harmless unsupported-host
  diagnostics, not maintained platform implementations.
- UTF-8/path-normalization boundaries: wrappers could be simplified later, but
  retain their validation contract and avoid a broad mechanical removal.

## Feature parity assessment

| Feature | Assessment at audited HEAD |
|---|---|
| Production queued file/folder/archive import and shutdown | **Blocked:** A09; direct facade tests do not exercise the queue. |
| Live thumbnails, covers, full-image viewer | **Not equivalent:** A06 after import/replacement. Reopen-only tests hide it. |
| Exact duplicates / rebuild thumbnails and video posters | **Unsafe or broken:** A05, including false identical classifications. |
| Gallery CRUD/order, tags/favorites/settings/search | Basic contract passes; casing/tag domain differs (A02/A10). Not full parity. |
| Password/keyfile rotation | Normal cold reopen passes; stale locked handles bypass the new wrap (A08). |
| Legacy conversion | Synthetic ordinary metadata blocks completion (A01/A02/A10); incomplete destinations are not gated (A07); cancellation gap A13. Source-preservation tests pass. |
| Encrypted random-access video playback | Existing v3 frame/authentication and real H.264 decode/seek tests pass. Full all-codec/audio/hardware/UI parity was not independently established. |
| Copy/move/combine and selective export | Backend-neutral facade paths inspected; existing tests pass. No new plaintext export sink found in the reviewed phase diff. Do not claim full v3 UI integration coverage. |
| Verify/GC/encrypted backup/restore | Existing corruption, snapshot and recovery tests pass; completeness and quick-open diagnostics remain deficient (A11/A12). |
| Large-vault responsiveness | Not established by delivered benchmark: A04. |
| Plaintext/key lifecycle | Secure content buffers and authenticated frame reads retained; new ordinary-heap tag copies violate the metadata boundary (A03). |

Intentional differences are not counted as defects: new `.osv` vaults are
directories; production legacy files are read-only and the main unlock flow
requires explicit copy-based conversion; v3 uses Verify/GC/database maintenance
instead of single-file compaction; PDF import exists in neither backend.
The retained legacy writer is a test/compatibility seam, not a supported
production editing path. Whole-vault rollback detection remains an explicitly
accepted limitation and is distinct from the stale live-header handling in A08.

## Baseline audit validation and coverage

- Read phase 110–115 delivery/acceptance documents, phase diffs, current vault
  memories, production call sites, and existing contract/integration tests.
- `scripts/test.sh`: **2,372 tests, 0 failed** on the audited source.
- Focused probes: **11 expected contract failures reproduced**. This is separate
  from the passing existing suite. Includes an isolated child-process abort;
  all data is synthetic. Source is preserved in
  [`docs/audit/phase-110-115-probes.cpp`](docs/audit/phase-110-115-probes.cpp).
- `scripts/test.sh --asan`: **2,372 tests, 0 failed**, exit 0, no
  ASAN/UBSan/LeakSanitizer finding on the approved unrestricted rerun. The first
  sandboxed run also passed all tests but failed LeakSanitizer's final thread
  inspection with a ptrace restriction; that run alone was not a clean gate.
  Local logs: `/tmp/osv-phase110-115-asan-unrestricted.log` and
  `/tmp/osv-phase110-115-probes.log`.
- No production fixes, compatibility removals, commits, pushes, or PR changes
  were made during the original audit. `.cache/` was pre-existing and left untouched.
- No manual SDL desktop session, exhaustive codec matrix, new 250k-media
  production benchmark, TSan rerun, or independent power-loss exercise was
  performed. Existing phase claims are not a substitute for those checks.

The probe executable links the unchanged Debug objects built by `scripts/test.sh`.
It is intentionally outside the regular test glob and exits nonzero until the
findings are fixed. Run from the repository root:

```sh
g++ -std=c++23 -g -DOSV_VENDORED_AV -DOSV_VENDORED_SQLCIPHER \
  -Isrc -Itests -Ivendor/monocypher/src -Ivendor/SDL3/include \
  -Ivendor/codecs-prefix/include \
  -c docs/audit/phase-110-115-probes.cpp -o /tmp/osv_audit_probe.o
g++ -o /tmp/osv_audit_tests /tmp/osv_audit_probe.o build/obj/Debug/osv_tests/*.o \
  build/bin/Debug/libmonocypher.a build/bin/Debug/libminiz.a \
  -Lvendor/SDL3/build -Lvendor/codecs-prefix/lib -Lvendor/.sqlcipher-build \
  -lSDL3 -ldl -lpthread -lm -lheif -lde265 -lwebpdemux -lwebp -lsharpyuv \
  -lavfilter -lavformat -lavcodec -lswscale -lswresample -lavutil -laom \
  -losv_vaapi_shim -larchive -llzma -lz -lsqlite3 \
  vendor/openssl-prefix/lib/libcrypto.a
/tmp/osv_audit_tests audit_
```

Probes retain their synthetic `/tmp/osv-audit-*` fixture directories for
inspection; they do not open or alter user vaults.
