# Legacy one-file vault converter (Phase 115)

**Status:** in progress

## Delivered so far

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
- Remaining work: conservative space preflight, bounded streaming for large
  videos, committed batches and progress, UI/credential flow, logical digest
  comparison, full legacy-fixture and crash/fault coverage, documentation and
  release gates.

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
9. After copying, compare structural counts, topology, names, metadata, tags/settings/searches, plaintext sizes, and a keyed streaming digest of every decrypted original between source and destination. Regenerate derived thumbnails/posters only when the source lacks a valid one, and record that fact.
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
