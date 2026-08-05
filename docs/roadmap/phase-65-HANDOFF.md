# Phase 65 — Cold Handoff

**Written:** 2026-08-05 · **Branch:** `phase-65-blocking-migration` · **Base:** `44728ad` (main)

This document assumes **zero context**. Read it top to bottom and you can resume without
reading the conversation that produced it.

---

## 1. What Phase 65 is

Two vault repairs currently run lazily *while the user browses*, so a `.osv` file mutates
on disk during ordinary reading:

- **`src/ui/video_repair.cpp:10`** (called from `GalleryGrid::refresh()`) — every video with
  `vmeta.codec == Unknown` is decrypted, re-probed, and given a **newly appended poster
  chunk** + index rewrite, **on the main thread**. A failed probe is memoised per session by
  `VideoMeta::probe_failed_session`, but that flag is never persisted, so an undecodable
  video pays a full read + decrypt + probe again on **every unlock**, forever.
- **`src/ui/anim_repair.cpp:9`** (called from `ImageViewer`) — images from pre-`INDEX_VERSION 7`
  vaults carry `animated = false` regardless of content; opening one re-sniffs and persists
  a correction mid-browse.

Phase 65 replaces both with a **one-time blocking, parallel, batch-committed migration**
offered at unlock, then deletes the lazy paths so the vault stops changing while browsing.

**Authoritative documents (both committed, read these next):**
- Design/spec: `docs/roadmap/phase-65-blocking-migration.md`
- Implementation plan, 11 tasks: `docs/roadmap/phase-65-implementation-plan.md`

---

## 2. Where the work stands

| Task | Status | Commits |
|---|---|---|
| 1 Watermark fields in `VaultSettings` (`INDEX_VERSION` → 10) | ✅ complete | `fbf0876` |
| 2 Detection module `vault/migration.*` | ✅ complete | `f64eb36`, `192bc68` |
| 3 Commit-free apply APIs on `Vault` | ✅ complete | `2c3f014`, `5ed3f82` |
| 4 `MigrationJob` single-threaded pass | ✅ complete | `ef281c9`, `11d1d28`, `7225cb9` |
| 5 Both arms end-to-end tests | ✅ complete | `1b7225e` |
| 6 Parallel decode pool | ✅ complete | `4d75ce2` |
| 7 Compaction phase | ⚠️ **fix applied, NOT re-reviewed** | `607de45`, `58c42b7` |
| 8 Unlock offer + progress modal | ⬜ not started | — |
| 9 Delete lazy repair paths | ⬜ not started | — |
| 10 Transfer watermark lowering | ⬜ not started | — |
| 11 Docs, ROADMAP, Serena memories | ⬜ not started | — |

**Ledger (the recovery map, survives context loss):**
`.superpowers/sdd/phase-65-implementation-plan/progress.md`
It records every task's completion commits, every fix round, and every deferred/parked
finding. Per-task briefs, implementer reports, and review diffs live in the same directory.
Trust the ledger and `git log` over anyone's recollection.

---

## 3. RESUME HERE

### 3a. FIRST ACTION: re-review Task 7's fix

**Task 7's fix landed as `58c42b7` but was never re-reviewed** — the session paused between
the fix and its scoped re-review. Task 7 is therefore **not complete**. This is the single
outstanding step before Task 8.

The implementer reported all three findings addressed (`scripts/test.sh` 1836 pass,
`--asan` clean), but that claim is unverified by review. Given that a Task 6 implementer
already reported a truncated TSAN run as a green gate (§4.2), do not take it on trust.

```bash
SKILL=~/.claude/plugins/cache/claude-plugins-official/superpowers/6.2.0/skills/subagent-driven-development
"$SKILL/scripts/review-package" docs/roadmap/phase-65-implementation-plan.md 607de45 HEAD
# then dispatch "$SKILL/re-review-prompt.md" with the printed diff path,
# the brief, the report file, and the three findings in §3b.
```

Verdict each finding ADDRESSED / NOT ADDRESSED, then append the outcome and a
`Task 7: complete (...)` line to the ledger.

### 3b. Task 7's three findings (to verdict in the re-review)

1. **(Blocking — owner already decided)** The `wasted_bytes()` sample must be **AFTER**
   `commit_migration()`. The implementer had sampled it *before*, which misses real waste:
   `vault.h:348` documents `wasted_bytes()` as "orphaned chunks from deletes **plus
   superseded index blobs**", and index blobs are appended, so the migration's own commit
   creates reclaimable waste a pre-commit sample cannot see.
   **The owner explicitly chose after-commit** when shown both tradeoffs. Do not revisit the
   decision — only verify it was implemented.
   *Consequence to check:* compaction will now run on essentially every migration, since
   every commit supersedes an index blob. If that proves heavy in practice, the follow-up is
   a size threshold — the plan's own wording ("skipped when `wasted_bytes()` does not justify
   the rewrite") implies one that no implementation actually has; both use `> 0`.
2. **(Important)** Both progress counters saved and restored. The prior code captured only
   `done` and restored `total = done`, leaving a misleading state if compaction fails.
   Also confirm the report names *which* pre-existing test required the restoration, so it
   reads as deliberate rather than as a workaround.
3. **(Minor)** `migration_job_compaction_reclaims_orphaned_chunks` must assert the vault
   **file actually shrank**, not merely that `reclaimed_bytes > 0`.

Also confirm no pre-existing test was weakened or deleted to accommodate the reordering.

### 3c. Then continue Tasks 8 → 11

Follow `docs/roadmap/phase-65-implementation-plan.md`. The execution method in use is
`superpowers:subagent-driven-development`: per task — extract a brief with `scripts/task-brief`,
dispatch a fresh implementer, generate a review package with `scripts/review-package`,
dispatch a task reviewer, run fix rounds until clean, then append to the ledger.

---

## 4. Hard-won lessons — read before dispatching anything

### 4.1 The plan's tests are too thin. This has bitten every task.

Tasks 2, 3, 4 and 7 **all failed review on the same defect**: the plan's tests only exercise
zero/no-op paths, so they pass against inverted predicates, dead I/O branches, and
unexecuted core loops. This is a flaw in the plan, not in the implementers.

**Every remaining dispatch must explicitly demand positive-path coverage with assertions on
observed state, not return values** — and must forbid conditional assertions
(`if (x) { CHECK(...) }`), which is how a test silently stops testing.

### 4.2 ⚠️ TSAN IS BROKEN ON THIS MACHINE — do not trust a green `--tsan`

`scripts/test.sh --tsan` **exits 66 and truncates at ~286 of ~1833 tests.** The cause is a
data race inside `/usr/lib/dri/radeonsi_drv_video.so` (the AMD VAAPI driver) reached via
`src/media/hw_accel.cpp` + `video_decode_worker.cpp`. It kills the test binary partway.

**This is PRE-EXISTING and not Phase 65's doing** — the phase touched exactly one file under
`src/media/`: `video_probe.h`, +7 lines.

Two traps this set:
- A Task 6 implementer reported **"288 PASS, 0 FAIL"** as a green gate. That 288 *is* the
  truncated run.
- My own first verification was also invalid: `scripts/test.sh --tsan 2>&1 | tail -60`
  returns **tail's** exit code, always 0. **Never pipe the sanitizer run** — capture `$?`
  directly.

**To actually run TSAN, disable the VAAPI driver:**
```bash
LIBVA_DRIVER_NAME=null LIBVA_DRIVERS_PATH=/nonexistent scripts/test.sh --tsan > log 2>&1
echo "exit=$?"
grep -c "SUMMARY: ThreadSanitizer" log     # must be 0
grep -c "RUN   migration_job" log          # must be 10
```
With this, **1144 tests ran, all 10 `migration_job` tests executed (including the 64-item
pool stress test), and TSAN reported ZERO races** — so the decode pool *is* verified
race-free. Allow >900 s; the fuzz tests are slow under TSAN.

**OPEN ISSUE FOR THE OWNER, outside Phase 65's scope:** the Phase 42 TSAN CI leg may have
been silently passing on a truncated suite on any machine with this driver. It needs a
driver suppression or an hwaccel-disable. `called_from_lib:radeonsi_drv_video.so`
suppressions were tried and did **not** work; disabling the driver via env did.

### 4.3 clangd diagnostics lag and lie

After every task, the editor reports `no member named ...` for symbols that exist and
compile fine. `compile_commands.json` is stale until `scripts/gen.sh` runs. **Verify against
the actual build and `git`, never the diagnostics.**

### 4.4 Build-system gotcha

Adding a file under `src/` needs a `premake5.lua` entry in the **`osv_tests`** project
(explicit source list). The `osv` app project globs `src/**.cpp` and needs nothing.
Tests under `tests/` are globbed (`tests/**.cpp`) and need no entry. Run `scripts/gen.sh`
after adding/removing/moving any source file.

### 4.5 Two constraints I stated too strictly to subagents

- I told subagents "`src/vault/` must not depend on `src/media/`". The accurate rule is
  ***headers*** under `src/vault/` must stay media-free — `vault.cpp` and `staging.cpp`
  already include `media/video_probe.h`. `VideoProbeApply` exists precisely so `vault.h`
  stays clean; that design is still correct.
- The skill wanted an isolated git worktree. We work directly on the branch instead,
  because a fresh worktree would need full submodule init and a complete vendored rebuild
  (FFmpeg, libheif, libaom, libwebp, SDL3, libarchive). The owner was informed.

---

## 5. Architecture as built (so you needn't re-derive it)

**Watermark** — `vault::VaultSettings` gained `migrated_index_version` (u8) and
`migrated_probe_caps` (u16), serialised at the tail of the settings block.
`INDEX_VERSION` 9 → **10**; pre-v10 blobs read `0/0` = never migrated. A watermark claiming
a *future* index version is **rejected on deserialise, not clamped** (the Phase 37/47/49 rule).

Staleness compares against dedicated constants, **not** `INDEX_VERSION`:
`vault::MIGRATION_INDEX_VERSION` (== 7, the version that added `animated`) and
`media::PROBE_CAPS_GEN` (== 1, bumped when decode capability expands). Comparing against
`INDEX_VERSION` would re-trigger the migration on every unrelated future format bump.

**Detection** — `src/vault/migration.*`: `scan_migration()` is a **pure tree walk, zero I/O**,
so the unlock-time offer is instant on any vault size. The image arm deliberately
over-counts (a static GIF is indistinguishable from an un-backfilled one without
decrypting); the watermark, not the scan, prevents recurrence.

**Apply APIs** — `apply_video_probe` / `apply_image_animated` / `commit_migration` are free
friends on `Vault`. They mutate **without committing**, so a whole pass costs ONE
`commit_index()` instead of one per node. `VideoProbeApply` mirrors `media::VideoProbeResult`
field-for-field to keep `vault.h` free of `media/`.

**The job** — `ui::MigrationJob` follows `FileOpJob`'s contract (**exclusive vault ownership
while `active()`**), *not* the Phase 50 staging contract — a blocking modal has no concurrent
browsing, so the coordinator may mutate the tree directly. One coordinator thread owns the
tree and all `fp_` writes; a pool of `max(1, hardware_concurrency()-1)` workers does the
CPU work (decrypt → probe/sniff → poster encode) reading **only** through the any-thread-safe
`vault::read_thumb_span`. Workers never hold an `IndexNode*`.

**The result queue is bounded** at ~`workers * 2`. This is a hard memory-safety requirement,
not style: decoded frames and posters live in `mlock`'d `SecureBytes` against a 256 MiB
process budget, so an unbounded queue exhausts the lockable pool and starts failing locks.

**Cancel** commits applied work (durable and correct) but must **NOT** stamp the watermark —
otherwise the migration is never offered again and whatever it skipped stays broken forever.
This was a real bug found in review; the fix re-reads the cancel flag before stamping.
**Do not regress it.**

**Crash safety** — nothing commits until the end, by design. A crash leaves the vault exactly
as it was; poster chunks already appended are dead ciphertext reclaimed by compact. Do not
"improve" this with incremental commits — it was an explicit decision.

---

## 6. Watch-outs for the remaining tasks

- **Task 8 (UI)** has **no automated tests** — a sanctioned waiver agreed with the owner and
  recorded in the plan's Global Constraints (this codebase has no headless screen harness).
  Gate it by manual verification via the `running-the-app` skill. Read the existing
  `FileOpJob` compact-modal polling pattern first; do not invent a second pattern.
- **Task 9 (deletion)** must verify the actual goal: with a migrated vault, `stat` the `.osv`,
  browse every gallery and open images/videos, then `stat` again — **size and mtime must be
  unchanged**. Also decide the fate of the now-superseded `Vault::repair_video_metadata` /
  `repair_image_animated` and of `VideoMeta::probe_failed_session` (never serialised, so
  removing it needs no format change) — only if nothing still reads them.
- **Task 10** closes a real hole that Task 9 opens: a node transferred in from an
  **un-migrated** vault lands in a vault whose watermark claims "done", and with the lazy
  paths deleted nothing would ever fix it. Enumerate **every** transfer entry point from
  `src/vault/transfer.h` — images, whole galleries, the bulk variant, combine. One missed
  path silently reinstates the hole.
- **Task 11** must run all three sanitizer legs before the PR — and see §4.2 for how to run
  `--tsan` truthfully.

**Deferred minors awaiting final-review triage** (also in the ledger):
- `tests/ui/test_migration_job.cpp` video test asserts `videos_fixed + videos_skipped >= 1`,
  which passes even when the video was *not* fixed. Task 5 strengthened the main video test;
  confirm no weak variant remains.
- Assorted clang-tidy nits across the new files (missing braces, lowercase literal suffixes,
  redundant test-seam declarations).

---

## 7. ⛔ Merge policy

**The OWNER merges. Never the agent.** When CI is green and SonarCloud is clean, post the PR
link, say it is ready, and stop. Do not run `gh pr merge`, do not push to `main`. Green gates
are permission to *ask*, never to merge. Keep follow-up fixes on this branch until the owner
has merged.
