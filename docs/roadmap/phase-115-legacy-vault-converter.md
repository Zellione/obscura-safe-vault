# Legacy one-file vault converter (Phase 115)

**Status:** complete

## Delivered

- Added a storage-layer converter that accepts only an unlocked legacy source
  and a distinct destination, creates a fresh v3 directory, and mints new vault,
  node, object, key, and nonce identities.
- Nested topology, sibling order, media metadata, timestamps, own tags,
  favorites, gallery sort keys, vault settings, and saved searches are copied.
  Originals and thumbnails/posters are authenticated from the source into
  locked/wiping buffers before being independently encrypted for v3.
- A non-secret source-header fingerprint and conversion version are stored in
  an encrypted `conversion_state` table. An interrupted run resumes only when
  that marker matches; unrelated existing destinations fail closed.
- Success requires deep object verification and a cold credentialed reopen.
  The marker is removed only after those checks, and tests hash the source
  before/after to prove it is unchanged.
- Large videos are converted through the v3 pull writer while retaining at
  most one authenticated legacy chunk in locked memory. A conservative free-
  space preflight and thread-safe item progress/cancellation counters are also
  in place.
- Conversion now commits deterministic 32-node prefixes. The encrypted
  conversion tables retain a stable traversal-identity to fresh v3 node-ID
  mapping (including pre-v13 sources that have no node IDs), so resume reuses
  committed object references and recopies only the uncommitted tail.
- A cold-reopened destination is compared against the source before the
  conversion marker is cleared: ordered topology, names, media metadata, tags,
  settings, saved searches, and every original's fresh random-keyed BLAKE2b
  digest. Video digests stream one authenticated source chunk / destination
  object frame at a time.
- Authenticating a legacy vault now transitions the unlock screen into an
  explicit conversion flow. The user chooses a distinct destination, password,
  and optional keyfile; cancellation and shutdown join the worker before the
  source handle can be destroyed. The app promotes only a successfully reopened
  destination, never the legacy source.
- The completion report shows item and byte totals, destination, verification
  results, derived-object warnings, and backup/manual-test guidance. There is no
  automatic source deletion.
- Publication fault injection covers write, file-sync, rename, and directory-
  sync failures followed by a safe retry. Wrong resume credentials fail as
  authentication, tampered source ciphertext reports only an opaque traversal
  ordinal, and a committed pre-framing golden fixture converts byte-exactly.
- Historical header/index generations remain covered by the reader/parser test
  corpus consumed by the converter; the golden fixture additionally exercises
  the oldest pre-framing object representation end to end.

## Goal

As the final phase, provide an explicit, non-destructive conversion from a legacy single-file `.osv` vault into a new v3 directory vault. The source is never modified or deleted automatically, and success is not reported until the destination has been deeply verified and reopened independently.

## User contract

- Conversion is copy-based, never in-place.
- The user chooses a distinct destination directory and supplies destination password/keyfile policy; reusing the source credentials is an explicit convenience, not an assumption.
- The legacy source stays byte-for-byte unchanged. Cleanup is a later manual user decision after they have tested and backed up the destination.
- Destination filenames are opaque object IDs; legacy node names go only into the encrypted database.
- Conversion may resume an application-created incomplete destination after source identity and format parameters are revalidated. It never resumes into an arbitrary existing vault.

## Step-by-step work

1. Add preflight: authenticate/unlock source, scan and validate its full index, calculate node/object counts and conservative destination space, validate destination parent, and reject source/destination aliasing or nesting.
2. Create the destination with a conversion marker inside its encrypted DB containing a non-secret cryptographic fingerprint of the legacy header/vault identity, source format generation, conversion version, and progress state. Do not store the source path or credentials.
3. Snapshot all logical source metadata into bounded secure work items. Preserve stable relationships, sibling order, timestamps, media metadata, own tags/favorites, inherited semantics, saved searches, category/template/value settings, sort settings, and migration watermarks where they remain meaningful.
4. Create fresh destination `vault_id`, node IDs, object IDs, nonces, database key, and master key. Never copy legacy ciphertext or identities directly because the v3 associated-data/key domains differ.
5. Convert galleries/topology in deterministic parent-first batches, recording a source-node-identity → destination-node-ID mapping in the encrypted destination DB.
6. For each original/thumbnail/poster/video: authenticate and decrypt from the source into bounded locked/wiping buffers, stream-encrypt into the new object format, durably publish, then commit its destination row. Videos stream chunk/frame-wise; never materialize a whole large video or write plaintext.
7. Commit resumable batches. After cancellation/crash, reopen both vaults, validate the source fingerprint and destination conversion marker, reconcile destination objects, skip only rows already committed and verified, and continue idempotently.
8. Treat source authentication/corruption as a hard stop with the exact logical item identified by opaque ordinal/ID in logs. Offer no "skip corrupt original" mode in the first version; partial silent conversion would undermine success claims.
9. After copying, compare structural counts, topology, names, metadata, tags/settings/searches, plaintext sizes, and a keyed streaming digest of every decrypted original between source and destination. Record missing derived thumbnails/posters as warnings; the normal v3 derived-media migration can regenerate them after conversion without weakening original-byte verification.
10. Clear the conversion-in-progress marker, checkpoint/backup the DB as specified, run v3 deep verification, close both vaults, then reopen the destination from scratch with destination credentials and sample/full-read according to the approved verification policy.
11. Present a completion report with source/destination locations, counts, bytes, verification result, warnings, and explicit guidance to back up and manually test before removing the source. Do not provide an automatic delete-source button in this phase.
12. Retain the legacy reader and converter in releases for an owner-approved deprecation window. Add fixtures for every supported legacy header/index generation rather than supporting only the newest legacy file.

## TDD, fault, and compatibility tests

- Golden legacy fixtures covering pre-framing, framed, pre/post domain-separated KDF, all index versions, contextless/context-bound records, images, animated formats, videos, deep trees, tags/templates/settings, and Unicode names.
- Logical equivalence comparison after conversion for every supported field and media byte stream.
- Wrong credentials, missing keyfile, hostile legacy index, tampered chunk, zero-byte media, duplicate names, maximum depth/count/size, low disk, destination collision, cancel, and shutdown.
- Crash injection before/after every object publication and batch DB commit; every case resumes idempotently or safely restarts into a new destination.
- Assert source file hash, size, timestamps where controllable, and bytes are unchanged after success and every failure.
- Cross-vault substitution tests confirm fresh identities and authentication failure.
- Full destination deep verify plus cold independent reopen; ASAN/TSan/Release/no-AV and CI/SonarCloud gates.

## Acceptance criterion

Every supported valid legacy fixture converts to a logically equivalent, independently keyed v3 directory vault; interruption resumes safely; corrupt input never yields a claimed-success destination; full verification and cold reopen pass; and the original one-file vault remains untouched for manual retention or deletion by the owner.

### Conversion form clarity

The conversion form identifies the already-unlocked source and explains that it
remains unchanged. Choose a parent folder first; the resulting new-vault path is
shown below the picker. Password and confirmation explicitly belong to the new
vault. Its optional keyfile controls are grouped: use an existing file, create a
file, or clear the selection with Use none. The selected keyfile path and its
requirement for future unlocks are shown together. Retrying an incomplete copy
requires the destination credentials from the first attempt.

The form uses font-derived spacing, bounded field widths, mouse selection of
password fields, and wheel scrolling for short windows. The redundant password
Copy button is omitted from conversion. Conversion errors distinguish source
state, destination conflicts, resume mismatch/authentication, source reads,
I/O, verification, cancellation, and the free-space/access preflight. Required
space is shown in MiB when an estimate is available. This changes presentation,
not the conversion format, verification checks, or conservative space policy.


## Conversion free-space estimate follow-up

The copy-only converter budgets one destination instead of requiring twice the
source/content size in additional free space. Its baseline is the larger of the
source file size and the sum of original media sizes plus stored thumbnail/poster
lengths. This preserves an allowance for legacy compression expansion. It adds
5% (rounded up), 16 KiB per node for metadata/filesystem overhead, and 32 MiB fixed
working slack. Every sum and multiplication is overflow-checked before creating
the destination. Source retention consumes no additional free space; verification
reads the destination in place.

This remains a conservative estimate, not a storage reservation or guarantee:
source dead space can overestimate the destination, filesystem allocation and
metadata vary, and resume still budgets a full destination. Runtime I/O failures
remain handled by the existing safe interruption/resume path.

Regression coverage exercises a sparse large source without a second-copy
budget, compressed media expansion, arithmetic overflow, and source preservation.

## Conversion diagnostics and performance audit

Conversion failures now retain the operation, backend status, first integrity finding
and opaque object ID, checked-object/authenticated-byte counts, and source DFS item
ordinal where applicable. Logical comparison identifies the differing metadata field,
settings group, original digest, or saved-search comparison. These diagnostics appear
in the conversion form and stderr without filenames, tags, passwords, or plaintext.
Long messages wrap and scroll without a four-line cutoff. A failure opening the final
browsing session is distinguished from failed content verification. Video destination
write failures are no longer automatically classified as source corruption.

The verifier uses one shared report-to-diagnostic path for both deep passes, including
checking corruption findings on the cold reopen. Redundant node-ID generation in
metadata cloning was removed; resume mapping remains responsible for checked ID creation.
The source remains read-only, and the incomplete destination stays gated until all
verification and finalization operations succeed.

### Performance findings (code audit; not a 500 GB benchmark)

This is the audit snapshot before the performance implementation described below.

1. **Growing full-tree commits are the largest metadata scalability concern.**
   `count_and_maybe_commit` commits every 32 nodes. `ReadSession::commit_metadata`
   calls `Database::sync_metadata`, which walks all accumulated nodes, updates
   sibling order, rebuilds tag/search joins, and revisits accumulated staged objects.
   The session then copies the full tree and reloads object references. Across N
   imported nodes this entails roughly quadratic aggregate metadata work, even
   though media copying itself is streaming. Prioritize a converter-specific
   incremental transaction API with atomic node/object/resume-map publication.
   Merely increasing batch size trades crash recovery granularity for speed and
   does not remove the scaling problem. Validate with 1k/10k/100k tiny-media cases,
   transaction counts, wall time, and interruption/resume tests before changing it.
2. **Verification rereads substantial data.** There are two deep destination scans
   (before close and after reopen), followed by original-content digest comparison
   that rereads the source and destination. For 500 GB of originals, verification
   alone reads approximately 2 TB of logical original data, plus derived objects
   and metadata. Physical I/O depends on compression and filesystem caches. A
   future fused cold verification/digest traversal could authenticate each object
   once while checking completeness and comparing source digests. Retain independent
   reopen, every derived-object authentication, and crash/corruption tests. This
   change retains the existing verification passes; no throughput gain is claimed.
3. **Resume space accounting remains conservative.** Preflight still asks for the
   full destination allowance before inspecting an incomplete copy. A nearly full
   disk can therefore reject a mostly completed resume. A future estimate must
   authenticate the resume marker and account only for valid reusable objects,
   remaining writes, and DB/WAL slack; subtracting directory size is unsafe.
4. **Resume rereads source thumbnails/posters before checking destination reuse.**
   Moving reuse lookup earlier saves reads but also removes a source-authentication
   check; preserve that coverage in the final verification before changing it.
5. **Durability and memory limits constrain parallelism.** Per-object durable
   publication and per-node resume mapping can dominate small-file workloads.
   Batch publication only with an explicit crash protocol. Keep video buffers
   bounded, and measure peak locked-memory use before adding concurrent copy jobs.
   Image copying and later digest comparison each materialize one full image.
6. **Progress currently counts copied nodes only.** Verification can occupy a long
   interval at the final node count. Follow up with explicit copy/verify/compare
   phases and byte progress, using atomics rather than reading the worker's mutable
   report from the UI thread.

The screenshot from a prior run cannot identify which old branch failed: the old
report discarded those details. These diagnostics apply to subsequent attempts.

## Conversion performance and dedicated progress screen

The converter now publishes parent-first batches of at most 32 nodes through a
conversion-only SQL transaction. Each transaction commits node rows, immutable
object references, and source-to-destination mappings together. Previously committed
nodes and object indexes are not rebuilt per batch. Tags, settings, and saved
searches are synchronized once at the end; the incomplete-conversion marker gates
browsing until final verification succeeds. Object files retain their existing
individual durable publication protocol. Interrupted, unreferenced publications
remain encrypted garbage and are collected on a successful resume.

Verification closes the writing session and independently reopens the destination
before one deep scan. That scan checks database/reference completeness, authenticates
every original and derived object, and computes ephemeral keyed BLAKE2b digests.
The logical comparison then authenticates source originals and thumbnails/posters
and compares those digests, topology, metadata, settings, and searches. This removes
the warm deep scan and the additional destination-original digest read: destination
objects are read once for verification. Digest keys are locked and wiped; retained
digests and hash state are wiped on every exit. Video buffers remain bounded to one
legacy chunk or destination frame; copying remains sequential.

Resume authenticates the matching encrypted marker and all referenced destination
objects before granting space credit. Only authenticated plaintext lengths reduce
the content allowance; the full per-node DB allowance and fixed working slack remain.
Directory size and orphan files never earn credit. Existing derived-object lookups
precede source reads; source authentication remains mandatory in final comparison.

While conversion is active, the unlock screen draws only the shared centered
operation-progress display, like legacy upgrades. Destination selection and password
fields are hidden. Atomic phase/byte counters distinguish preparation, copy, cold
reopen, authentication, source comparison, and finalization. Escape requests safe
cancellation; errors return to the form with the existing diagnostics.

Regression coverage includes atomic batch rollback/idempotency, cancellation at a
committed batch and within video/verification frames, authenticated resume space
credit, corrupt reused source thumbnails, destination tampering, and phase text.
The opt-in `OSV_CONVERSION_BENCH=1000|10000|100000` tiny-media fixture reports conversion
wall time and batch count; it excludes source construction and does not represent
large-video throughput or peak locked-memory usage.

Local Release measurements on tmpfs (with a concurrent sanitizer build): 1,000
three-byte originals: 0.542 s / 32 batches; 10,000: 6.162 s / 313 batches; 100,000:
129.022 s / 3,126 batches. All conversions passed cold and logical verification.
These are absolute measurements, not a before/after speedup claim. The largest
fixture exceeded the process's lockable-memory allowance; no parallel copy workers
or increased plaintext buffering were introduced.
