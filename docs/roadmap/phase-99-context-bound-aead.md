# Context-bound AEAD and vault-format migration (Phase 99)

**Status:** ✅ shipped
**Date:** 2026-08-31

This is **Phase F** of the 2026-08-29 security audit remediation
(`AUDIT.md` / `AUDIT_IMPLEMENTATION.md`, phases A–G mapping to app phases
94–100). It fixes **OSV-AUD-004** (Medium — AEAD ciphertext is not bound to
logical chunk identity, CWE-345). This phase is a **format change**: the only
audit phase that requires one. Delivered as **two PRs**: PR 1 (primitives +
v13 context writer, legacy vaults still readable) and PR 2 (the v1→v2
migration).

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
  `create`; legacy vaults get it set by the v1→v2 chunk migration (PR 2), in
  the same atomic commit that re-seals the master-key wrap.
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

## PR 2 (shipped) — v1→v2 migration

* **Detection:** a legacy vault owes the context migration whenever its header
  `FLAG_CONTEXT_BOUND_CHUNKS` is clear (`vault::uses_context_chunks`). The
  per-record `context_bound` bit (v13 index) tracks per-node progress, so a
  cancelled run resumes where it stopped.
* **Rewrite:** `vault::apply_context_rewrite` re-encodes ONE media node's
  records in place on the coordinator (no worker pool, no decode): decrypt
  each v1 record → re-append as a v2 record with a fresh per-record id + the
  node's id (minted if the legacy node has none) + preserved video sequence.
  Idempotent per node; a crash mid-node re-runs from the last commit (the
  half-rewritten node's legacy spans are still valid in the committed index).
  The `MigrationJob` runs the context rewrites BEFORE the decode pool so the
  pool's thumb/poster appends go straight to the context-bound AEAD.
* **Finalize:** `vault::finalize_context_migration` re-seals the master-key
  wrap under the **session KEK** (captured at unlock/create/change_password
  into `Vault::kek_`, wiped at lock — no password re-derivation needed), mints
  a `vault_id` if the legacy header has none, and sets the header flag. The
  migration's one `commit_index()` then writes an AD-sealed index blob, and
  the slot-swap persists the flag + wrap + vault_id + slot **atomically**:
  a crash before phase B leaves the legacy header (recovered → re-offered), a
  crash between B and C recovers via the existing slot-fallback. Cancel
  commits the rewrites done so far but skips finalize (flag stays clear, the
  vault re-opens on per-record bits and is re-offered).
* **Watermark:** `stamp_migrated` runs as before; the flag is the context
  watermark, and the unlock-time offer detects and finalizes an already
  rewritten-but-unfinalized vault instead of re-offering forever.
* **Tests** (`tests/vault/test_migration_context.cpp`, + a `test_only_downgrade
  _to_legacy` seam that converts a fresh v2 vault into a genuine legacy one):
  scan counts legacy records only; rewrite+finalize+reopen round-trips content
  and the re-wrapped wrap/index; a post-migration equal-record swap → AuthFailed
  (context now active); a cancelled (mixed) vault reopens with both legacy and
  context-bound records readable and the scan reporting the remainder; video
  chunks keep 0-based sequences and re-read byte-identically.

### Verification

* `scripts/test.sh`: **2262 tests / 0 failed**.
* `--asan`, `--tsan`, `--release`: 2262/0 each; no-FFmpeg leg 2076/0.
* `git diff --check` clean. Windows CI + SonarCloud: PR #218.

## Deliberately unchanged

* **No plaintext to disk** — migration decrypts into mlock'd `SecureBytes`.
* **`compact()`** stays byte-verbatim (physical offsets never enter the AD).
* **Whole-vault rollback** from a complete old backup remains out of scope
  (needs an external monotonic trust anchor).