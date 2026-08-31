# Context-bound AEAD and vault-format migration (Phase 99)

**Status:** 🔜 in progress — **PR 1 shipped** (primitives + context writer,
legacy vaults still readable); **PR 2 pending** (v1→v2 chunk migration +
watermark + compaction of dead v1 records).
**Date:** 2026-08-31

This is **Phase F** of the 2026-08-29 security audit remediation
(`AUDIT.md` / `AUDIT_IMPLEMENTATION.md`, phases A–G mapping to app phases
94–100). It fixes **OSV-AUD-004** (Medium — AEAD ciphertext is not bound to
logical chunk identity, CWE-345). This phase is a **format change**: the only
audit phase that requires one.

## The finding

Every data/thumbnail/poster/video chunk and the index blob were authenticated
under the master key with only a random nonce. Nothing bound a record to *what*
it was: two equal-length records could be swapped inside the file (each keeps a
valid tag under the shared master key) and the authenticated index would
silently point at the substituted content — most practical for video chunks,
which regularly share plaintext/ciphertext length. The container detected bit
flips but not substitution.

## Design (approved by the owner)

* **Format signal:** a new header flag `FLAG_CONTEXT_BOUND_CHUNKS` (bit 2) on
  the existing v1 header (not a `FORMAT_VERSION` bump). New vaults set it at
  `create`; legacy vaults get it set only once the v1→v2 chunk migration has
  rewritten every live record (PR 2).
* **Stable identity:** every node (media **and** gallery) carries a persistent
  random 128-bit `node_id`; every chunk record carries its own random 128-bit
  `record` id; video chunks carry a logical `sequence`.
* **AD canonical encoding** (fixed-width little-endian, deterministic on every
  platform — see `mem:vault_format`):
  `domain u8 (DATA/THUMB/POSTER/VIDEO/INDEX/MK-WRAP) | version u8 | owner(16:
  node_id, or the vault_id for Index/MkWrap) | record(16) | sequence u32 LE` =
  38 bytes. Physical offsets are deliberately **not** in the AD so `compact()`
  keeps moving ciphertext byte-for-byte.
* **Immutable AD owner:** a new header `vault_id` (16 bytes in the reserved
  region) is the owner of the index-blob and master-key-wrap AD — **not** the
  salt, because `change_password` regenerates the salt and would otherwise
  invalidate the legitimately-sealed index blob.

## PR 1 (shipped) — primitives + context writer

* `crypto/aead.h` `ChunkDomain` / `ChunkTag` / `build_chunk_ad` (+ exact-byte
  KAT and domain/identity-binding tests).
* `crypto::NODE_ID_SIZE` (16); header `FLAG_CONTEXT_BOUND_CHUNKS` + immutable
  `vault_id`.
* `INDEX_VERSION = 13`: `IndexNode::node_id`, `ImageMeta::data_id`/`thumb_id`
  + `context_bound`, `VideoChunk::sequence`+`id`, `VideoMeta::poster_id` +
  `context_bound`. Pre-v13 blobs read zero ids / `context_bound=false` (all
  records legacy). Reject-not-clamp on the new 0/1 bits.
* `ChunkStore::append_chunk`/`read_chunk` now require a `crypto::ChunkTag`;
  appends regenerate a fresh random `record` id when context-bound; reads
  derive byte-identical AD from the authenticated index alone.
* All write paths (import/staging, transfer/combine, `apply_video_probe`,
  `apply_image_thumb`, `apply_video_poster`) and read paths (`read_image`,
  `read_thumbnail`, `read_video`, `VideoSource`) thread the identity; a fresh
  node identity is assigned at the destination on cross-vault transfer.
* UI span snapshots (gallery covers, duplicate scan, migration workers) carry
  `vault::ChunkRef` values (span + identity) so their any-thread reads never
  hold a dangling `IndexNode`.
* **Legacy vaults stay contextless** (flag clear / `context_bound=false`): no
  AD on read or append.

### Security-behavior tests (PR 1)

* Swapping two equal-size image records, or two adjacent same-size video chunk
  records, physically in the file → both reads `AuthFailed`.
* Replaying a record under a different per-record id / sequence / role →
  `AuthFailed`.
* In-place `compact()` keeps every surviving chunk authenticating (verbatim
  ciphertext moves).
* Cross-vault transfer assigns a fresh destination node identity and the
  content still authenticates there.

### Verification (PR 1)

* `scripts/test.sh`: **2257 tests / 0 failed** (baseline 2248 + 9).
* `scripts/test.sh --asan`: 2257/0 clean.
* `scripts/test.sh --tsan`: 2257/0 clean, no new races.
* `scripts/test.sh --release`: 2257/0.
* No-FFmpeg leg (`--no-av`): 2071/0.
* `git diff --check` clean. Windows CI + SonarCloud results recorded on the
  Phase 99 PR 1.

## PR 2 (pending) — v1→v2 migration

* Reuse the Phase 65 `MigrationJob` pattern: decrypt one record into
  `SecureBytes` → re-encrypt a v2 record with a fresh `record` id + context →
  append → update an index copy → bounded crash-safe batch commits. A
  per-record `context_bound` bit (already in the index) keeps the reader
  correct while the active index references a mix of v1/v2 records.
* Assign a `vault_id` (if zero) and re-wrap the master key with the MkWrap AD
  in the same commit that sets `FLAG_CONTEXT_BOUND_CHUNKS`.
* Stamp a `migrated_context_version` watermark only when all live records are
  v2, then `compact()` the dead v1 ciphertext.
* Cancel/crash/reopen must land in a valid resumable partial state; fuzz both
  the v1 and v2 readers.

## Deliberately unchanged

* **No plaintext to disk** — migration decrypts into mlock'd `SecureBytes`.
* **`compact()`** stays byte-verbatim (physical offsets never enter the AD).
* **Whole-vault rollback** from a complete old backup remains out of scope
  (needs an external monotonic trust anchor).