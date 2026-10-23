# Phase 122 — Vault organisation, responsive bulk operations, and VP8 reliability

Status: implementation in cascading PRs. A and H's storage documentation are in
[PR #261](https://github.com/Zellione/obscura-safe-vault/pull/261), ready for owner
review with all CI gates green and zero SonarCloud issues. Local Debug and Release
each passed 2,497 tests. The remaining H validation is not marked complete.
B is in [PR #262](https://github.com/Zellione/obscura-safe-vault/pull/262), based on A.
Local Debug and unrestricted ASAN/UBSan/LSan each pass 2,502 tests. CI is green; ten Sonar findings are being addressed. D is in [PR #263](https://github.com/Zellione/obscura-safe-vault/pull/263), stacked
on B. G is [PR #264](https://github.com/Zellione/obscura-safe-vault/pull/264) with deterministic VP8 color/fallback regression coverage and
README remediation guidance; the reported NVIDIA hardware failure remains
unreproduced. See [G evidence](phase-122g-implementation.md).

F follows G with bounded import drains, producer backpressure, background v3
saves, and phase progress. See [measurements and limits](phase-122f-implementation.md).

### B implementation findings

The selection editor previously listed and sorted an entire parent gallery once
per selected path, then synchronously saved the full metadata snapshot. Selection
resolution now indexes each parent once. Loading, mutation, save, and refresh run
on a worker while the owning screen renders progress and gates tree access.
The import queue is held exclusive for the editor session; App does not drain it
while a screen's worker owns the tree. Teardown joins before releasing ownership
or wiping vault keys. Tag edits restore live assignments on pre-save cancellation
or failed commit. The full v3 metadata transaction remains; this change does not
claim incremental SQL updates or a constant-time save.

Initial synthetic Debug measurements (flat gallery nodes to isolate metadata,
not representative media-import throughput): 1k nodes: open call 0.12 ms,
background add/save 78 ms; 10k nodes: open call 0.93 ms, background add/save
1.90 s. At 50k nodes, open returned in 1.69 ms, add returned in 0.04 ms,
background add/save completed in 3.95 s and removal in 3.50 s. A prior run under
concurrent compilation exceeded the initial 10-second completion wait; these
figures are indicative measurements, not storage throughput guarantees. `OSV_TAG_SELECTION_BENCH` runs 1k/10k/50k
scales and separately reports UI call duration and background completion.

## Goal and current evidence

Make everyday organisation and large imports responsive, preserve tag meaning
across renames and bundles, and resolve VP8/WebM green playback. Explain the
existing storage design before considering another migration.

Source inspection establishes these starting points:

- `GalleryGrid::start_rename` and `RenameDialog` already rename individual
  galleries, images, and videos using R on the focused tile. Document the hover
  interaction after verifying pointer-to-focus behavior; do not introduce a
  second single-item rename implementation.
- `TagEditor` calls `add_tag_batch` / `remove_tag_batch` synchronously. Existing
  batching does not establish that large selections keep the event loop responsive.
- Imports already have a worker queue, staged attachment, and commit batching.
  Phase 121 adds forward archive decoding. Measure the production path rather
  than assuming that adding threads alone will solve the reported slowdown.
- Phase 118 implements global individual-tag rename, but explicitly excludes
  category definitions. Category rename must update the associated vocabulary.
- V3 already stores independently encrypted content objects and encrypted database
  metadata. The legacy single-file format is read-only. See H below.

These are code observations, not a reproduced diagnosis of the performance or
VP8 reports. Existing phase statuses do not replace checking their integration
before implementation.

## Sequence

Start with A and H (interaction/storage documentation), then B (baseline and
shared mutation behavior), C (bulk rename), D (category rename), and E (meta
tags). F (imports) and G (VP8) can proceed independently once their baselines
are captured. Finish with the integrated acceptance checks. Keep implementation
in cascading subphase branches, each PR targeting its predecessor, as requested
by the owner. Each PR carries its validation and CI/Sonar gates; the owner merges.

## 122A — Discoverable single-item rename

- Document in README and F1: hover a gallery name/tile, press R, edit, Enter
  saves, Esc cancels. Also document keyboard focus and image/video renaming.
- Verify hover and keyboard targeting agree, including after scrolling. If they
  differ, fix focus targeting first; avoid describing unimplemented behavior.
- Preserve text-input shortcut isolation, name validation, selection identity,
  and read-only legacy behavior. Ensure the modal clearly identifies its target.

Acceptance: pointer and keyboard regression cases rename the intended item,
persist across reopen, and leave content objects unchanged. README/F1 agree.

## 122B — Responsive selection tagging and mutation infrastructure

- Reproduce add/remove on 1k, 10k, and 50k media selections, including mixed
  parents and inherited tags. Record total time, event-loop gaps, allocation
  peaks, SQL transaction counts, snapshot cost, and list/cache rebuild time.
- Trace whether full-tree persistence, repeated lookups, per-item commits, or
  UI refresh dominates. Prefer targeted metadata transactions and one refresh
  per operation where evidence supports them; preserve durability settings.
- Prepare changes off-thread using detached secure data or an exclusive job
  with an explicit vault ownership handoff. Never let the UI traverse a tree
  while its worker mutates it. Bound main-thread work, including preparation
  and completion; putting only the final commit on a worker is insufficient.
- Show preparation/apply/save progress and counts. Use an indeterminate indicator
  when totals are unknown. Continue rendering/input; gate conflicting mutations.
- Prefer one atomic transaction for a selection edit: cancel before commit leaves
  no changes, failures restore live state, and cancellation becomes unavailable
  during final durable commit. If measurements require partial batches, specify
  and display committed counts explicitly before adopting that alternative.
- Join/cancel before lock, close, vault switch, or shutdown; report save failures.

Acceptance: on a recorded reference machine, progress appears within 200 ms and
routine UI event-loop gaps stay below 100 ms during the above workloads. Record
before/after timings; any missed budget needs a documented cause and follow-up.
Test reopen, cancellation, injected commit failure, and teardown. A progress bar
that stops repainting does not meet acceptance.

## 122C — Selection rename and regex mass rename

- Reuse single-item rename; add an explicit bulk action for selected images,
  videos, and galleries in a writable vault. Recursion is opt-in and previewed.
- Preview old/new names, unchanged and unmatched items, invalid results, and
  collisions. Specify regex dialect, replacement captures, case sensitivity,
  first/all-match behavior, and basename versus extension handling in the UI
  and README. Preserve extensions by default for media.
- Compile once and use a bounded-time regex strategy; a cancellable UI wrapper
  around an uninterruptible pathological match is insufficient. Choose the
  engine after reviewing existing dependencies; any new dependency follows
  vendoring and security-review rules.
- Reject empty/unsafe/oversized names and resolve the whole planned final sibling
  namespace before applying. No silent overwrite or merge. Cover swaps, cycles,
  case-only renames, existing case-distinct siblings, and parent+child selections.
- Track targets by stable identity, not paths that earlier renames invalidate.
  Apply atomically using B, revalidate stale previews, and refresh affected views.

Acceptance: tests first for captures, invalid expressions, adversarial patterns,
Unicode names, collisions, swaps, rollback, and reopen. Preview exactly matches
the committed result; object identities and ciphertext remain unchanged.

## 122D — Rename a category everywhere

Implemented for validation: transactional prefix rewrite covers category names,
root/gallery/media tags, descriptions' keys, field-value keys, and typed saved
query clauses. Case-only renames work. Any existing target namespace or malformed
saved query rejects the whole operation; prose remains unchanged. F2 Tag Colours
launches a background operation with progress, gates pending navigation/import
attachment, joins before vault teardown, and refreshes settings without repeating
full object verification for this metadata-only change. Bundle references are an
integration requirement for E, not yet delivered in D. Local Debug and unrestricted ASAN/UBSan/LSan: 2,506 tests each, zero failures.


- Add a transactional vault operation behind category editing. Match the actual
  category prefix boundary, not arbitrary substrings (e.g. `person:Alice` changes
  when `person` changes; unrelated text containing that word does not).
- Update category definitions, all node assignments, metadata-only tags,
  descriptions' tag keys, template associations, field-value tag keys, and typed
  tag/category references in saved searches. Preserve descriptions and other
  free text verbatim. Include meta-tag references once E exists.
- Preserve swatches, field values, ordering, and tag identity where supported.
  Apply existing case-insensitive identity rules; allow case-only rename and
  reject ambiguous target categories/tags rather than silently merging them.
- Reuse Phase 118 semantics where appropriate, but perform one atomic operation
  rather than independently committing a rename for each member tag. Detect
  malformed dependent queries before applying; report blocked references rather
  than claiming a complete rename while leaving them stale.

Acceptance: no old structured category reference remains after reopen; unrelated
names and free text are unchanged. Tests cover root/nested nodes, metadata-only
tags, saved queries, collisions, and failure rollback. Large renames satisfy B.

## 122E — Meta tags with persistent assignment provenance

- Provide named, vault-local bundles of ordinary tags, editable in administration
  and applicable/removable through selection tagging. Initially disallow nested
  bundles so cycle/recursive expansion semantics are not implicit.
- Persist explicit assignment separately from each bundle's contribution. A tag
  is locally effective while explicitly assigned or supplied by any active bundle;
  ancestor inheritance continues to be calculated separately.
- Thus a pre-existing manual tag survives bundle removal; two overlapping bundles
  do not remove each other's tags; manually adding a bundle-supplied tag makes
  it survive later removal. Removing a manual assignment does not hide a tag
  still supplied by an active bundle or ancestor; show its source in the UI.
- Bundle edits preview their effects on existing applications and update those
  contributions atomically. Bundle deletion removes only its contributions.
  Stable bundle/tag identity must survive tag and category renaming.
- Add a versioned encrypted database migration: existing assignments become
  explicit, no content-object rewrite. Define old-client rejection, backup/restore,
  integrity checks, conversion, and cross-vault transfer behavior. Transfers must
  preserve provenance and explicitly resolve bundle-definition conflicts.
- Keep definitions and provenance in secure metadata; no plaintext sidecars.

Acceptance: table-driven tests cover pre-existing tags, later manual additions,
overlapping bundles, inheritance, repeated apply/remove, edits/deletion, rename,
transfer, migration/reopen, and interrupted transactions. Search and counts use
the same effective-tag semantics as display. Large operations satisfy B.

## 122F — Folder and archive import throughput and responsiveness

- Benchmark folders, ZIP/CBZ, TAR, solid/encrypted 7z/RAR, multipart sets, and
  nested archives, with small-image and mixed large-image/video workloads.
  Compare cold/warm storage and existing small/large destination vaults.
- Time discovery, archive opening/password probes, extraction, decode/poster
  generation, encryption/publication, staged attachment, metadata commit, and
  refresh. Record queue depth, per-core utilization, I/O wait, throughput, and
  secure-memory high-water marks. Low total CPU alone is not a diagnosis.
- Check repeated solid-archive passes, full-source copies, serial decode,
  per-file fsync/metadata work, quadratic gallery lookup, queue starvation, and
  oversized UI drains. Fix demonstrated bottlenecks, preserving publication
  ordering and crash consistency; never disable authentication or durable sync.
- Use bounded producer/decode queues and measured concurrency. Keep solid
  extraction sequential where required while overlapping independent downstream
  work. Respect secure-memory budgets and cancellation; no disk extraction.
- Report discovery/extraction/processing/saving phases, bytes where available,
  attached/imported counts, skips, failures, and cancellation. Unknown totals
  must not look like a hung 0% bar. Budget each main-thread drain and refresh.

Acceptance: publish reproducible before/after measurements and bottleneck fixes,
meet B's UI budget, and show no material regression on representative workloads.
Verify deterministic placement/counts, passwords, corrupted/nested archives,
low memory, cancellation, crash/reopen, and lock/shutdown lifetime safety.

## 122G — VP8/WebM green-output diagnosis and remedy

- Obtain or generate a redistributable failing fixture; compare the same file
  through software and hardware-with-fallback paths. Record build, GPU/driver,
  selected decoder, pixel format, plane strides, dimensions, and color metadata
  without logging user content. Reproduce before choosing a remedy.
- Isolate demux/decode, hardware download, pixel conversion, frame lifetime,
  and SDL upload. Exercise odd sizes, padded strides, seek, reopen, and fallback.
  Preserve secure buffers, swscale padding, and final-reference wipe ownership.
- Fix application errors with regression tests. If a hardware-specific path is
  unusable, assess a scoped software fallback instead of disabling all acceleration.
- If the cause cannot be fixed in-program, put a verified user workaround in
  README: test F2 Playback → Software first, explain exactly which configurations
  it addresses, and provide driver/package instructions only after reproducing
  and verifying them. Do not promise that software fixes every green frame.
  Do not recommend exporting decrypted content as the routine workaround.

Acceptance: fixture decodes with expected reference colors through the supported
  path, including seeks, or a reproduced platform limitation has tested README
  remediation and clearly documented remaining scope. No diagnosis is claimed
  merely from the green-output symptom.

## 122H — Storage layout review and upgrade resilience

The current implementation already realizes most of the requested design:

```text
example.osv/
  vault.header                 wrapped key and format/KDF parameters
  vault.db                     encrypted names, hierarchy, tags, object mapping
  objects/<two-hex-digit-shard>/<random-object-id>.osvo
                               independently encrypted content objects
```

Originals, thumbnails, and posters have separate immutable object files. A video
is one object with internally authenticated frames for random access. Galleries
are database nodes, not encrypted files containing their children. Random IDs
are opaque names, rather than ciphertext encodings of user filenames. See
[v3 format](../V3_VAULT_FORMAT.md), [object store](phase-108-encrypted-object-store.md),
and [architecture decision](../adr/0001-directory-vault-sqlcipher.md).

Compared with physically mirroring gallery folders under encrypted names:

| Concern | Current sharded objects | Physical gallery hierarchy |
|---|---|---|
| Metadata exposure | Counts, lengths, shard layout, timing remain visible | Also reveals gallery grouping, depth, and movement despite encrypted names |
| Rename/move | Database-only; content stays put | May require coordinating directory moves with database updates |
| Crash recovery | Publish immutable objects before committing references | Extra recovery states when filesystem hierarchy and database disagree |
| Upgrade corruption | Metadata migrations need not rewrite media | Also separates media, but directory nesting does not protect database mappings |
| Filesystem cost | Many object files, bounded shard directories | Many files plus gallery directories, uneven fan-out and deep-path handling |
| Manual recovery | Opaque files require metadata mapping | Grouping may help salvage, but names/tags still depend on decryptable mapping |

The proposed physical tree is feasible, but is not required to isolate media
corruption. Both designs retain a critical metadata database: losing its mapping
can lose names, hierarchy, tags, and discoverability even if media bytes survive.
Shared storage failure or a faulty migration can affect either layout; independent
files do not replace consistent encrypted backups and restore testing.

Recommendation for this phase: retain the current object layout and focus on
transactional schema migrations, verified encrypted backups, integrity checks,
and crash-injection tests proving metadata upgrades leave object bytes untouched.
Document the actual layout and recovery limits in README, and clearly label the
legacy container reference in ROADMAP as legacy. Review representative vault
metadata/object counts using synthetic data; do not infer gallery packing from
filesystem appearance. A physical-hierarchy change would require a separate ADR
and owner decision, not an incidental rewrite during these UI features.

Acceptance: documentation matches implementation; upgrade failure/reopen tests
prove old-or-new metadata consistency and unchanged original objects; encrypted
backup/restore exercises establish the supported recovery boundary.

## Delivery gates

Write behavior/regression tests before implementation. Run `scripts/test.sh` and
`scripts/test.sh --asan` for vault/memory changes, plus relevant no-AV and thread
lifetime checks. Include UI responsiveness measurements and manual pointer/F1
checks that unit tests cannot establish. Update README, format/architecture docs
where changed, ROADMAP, and affected Serena memories to reflect delivered state.
CI must pass and PR SonarCloud findings must be resolved and rechecked. Hand off
the ready PR to the owner; never merge it. This planning document does not mark
any requested feature or fix as delivered.
