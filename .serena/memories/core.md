# obscura-safe-vault — Core (memory graph root)

Multi-platform (Linux → Windows; no macOS) encrypted photo gallery. A single `.osv` vault
file. Application-owned decrypted data lives only in `mlock`'d, wipe-on-release heap;
opaque codec/driver buffers are minimized and surfaced as a degraded F1 status (Phase 97).
Plaintext is never written to disk except the one gated deviation (`ui::export.*`). Galleries freely nest and may hold any mix of images,
videos, and sub-galleries as direct children (no leaf-only restriction — sub-galleries display
first, then media).

This is the graph root: it holds the high-level map + cross-cutting invariants, and points to
the per-domain memories below. Read the module memory for whichever `src/` directory you're
working in.

## Source map (directory → module memory)

```
src/
  app/       state machine + SDL event loop, single-active vault, idle auto-lock, session state
  platform/  config dirs, file dialogs, vault registry, theme/volume prefs, hardening, error log
  crypto/    Monocypher wrappers (XChaCha20-Poly1305, Argon2id, CSPRNG, SecureBytes/mlock)
  vault/     .osv container: index, chunk store, transfer/combine, search, safe node names
  image/     stb/WebP/HEIF decode, thumbnails, off-thread decode worker
  media/     FFmpeg video/audio decode over encrypted chunks, hw accel, A/V sync (OSV_VENDORED_AV)
  gfx/       SDL3 window/renderer, texture + YUV textures, text atlas, themes
  ui/        all screens, image/video viewer, dialogs, pure view/search/sort/session models
tests/       crypto/ gfx/ image/ platform/ ui/ vault/ media/ + test_framework.h, test_main.cpp
vendor/      git submodules — pinned versions, build mechanics, CI matrix in mem:tech_stack
```

- `app/` + `platform/` — App lifecycle, event loop, vault ownership + auto-lock, config-dir
  persistence, hardening, diagnostics: **`mem:module/app`**.
- `crypto/` + `vault/` — storage & security core: the `.osv` format, index tree, chunk
  framing, transfer/combine/search, node-name safety: **`mem:module/vault`**.
- `image/` + `media/` — all decode: image codecs + thumbnails, FFmpeg video/audio, hardware
  accel: **`mem:module/media`**.
- `gfx/` — SDL rendering primitives, texture caches, text, themes: **`mem:module/gfx`**.
- `ui/` — every Screen, the image/video viewer, all dialogs, and the pure SDL-free
  view/search/sort/session models: **`mem:module/ui`** (index) with sub-memories for
  text-input, screens, viewer, dialogs, models, jobs, and import.

## Project-wide invariants (NEVER violate)

1. No decrypted bytes to disk. Application-owned plaintext uses best-effort `mlock`'d,
   wipe-on-release heap buffers; opaque third-party codec/driver allocations that cannot accept
   caller storage are documented and must set the F1 degraded status (Phase 97).
   EXCEPTION (documented, gated): `src/ui/export.*` deliberately writes decrypted originals to
   disk on explicit user consent (selection-only, never thumbnails, buffer wiped right after
   write, and since Phase 98 the sink is an ATOMIC no-follow create —
   `platform::create_new_file_within`, see `mem:module/app`). No other path may write plaintext.
2. All key/KEK/password buffers wiped with `crypto_wipe` before free.
3. Every XChaCha20-Poly1305 encrypt call uses a fresh 24-byte CSPRNG nonce.
4. Tag verified before any plaintext bytes are consumed.
5. Keys, passwords, decrypted content must never appear in log output.
6. A vault file is untrusted input (portable/shareable); node names are path components, never
   paths — validate on ingress (`vault::is_safe_node_name`), repair on import
   (`vault::sanitize_node_name`), and claim atomically on export
   (`platform::create_new_file_within` — Phase 98: no-follow, containment-enforced, the write
   goes through the already-open handle, never a checked-earlier path; `ui::export_path_within`
   remains only as a post-create assertion); never
   build `dest_dir / node.name` directly (CWE-22). See `mem:module/vault` safe_name.*.

## Key hierarchy

`KEK = Argon2id(domain || len(password) || len(keyfile) || password || keyfile, salt)`
→ unwraps a random 32-byte master key. Legacy vaults retain the original ambiguous
`password || keyfile` encoding until the next successful password change, which migrates
the wrap to the domain-separated encoding.
All data/thumbnail/index chunks are encrypted with the master key + a fresh nonce per chunk.
Since **Phase 99 (OSV-AUD-004)** every record's AEAD is context-bound: the AD carries a
domain tag (DATA/THUMB/POSTER/VIDEO/INDEX/MKWrap), the node's persistent 128-bit
`node_id`, a fresh per-record 128-bit `record` id, and (video only) a logical `sequence`
— so equal-length record swap/replay/splice (and any substitution of a complete
ciphertext record) fails authentication. Physical offsets stay OUT of the AD so
`compact()` moves ciphertext byte-for-byte. `INDEX_VERSION` is 13; new vaults set the
header's `FLAG_CONTEXT_BOUND_CHUNKS`, legacy vaults gain it via the one-time v1→v2
migration (the per-record `context_bound` bit fuses the window). Full byte layout:
`mem:vault_format` "Context-bound AEAD".

Since **Phase 91** the index tree's human-readable metadata is also secure: node
names, tags, category names, tag descriptions, tag field values, and saved-search
names are `crypto::SecureString`, while encoded saved-search queries use
`crypto::SecureBlob` (see `src/crypto/secure_string.h` and `mem:module/vault`) —
mlock'd best-effort + `crypto_wipe`'d on destroy. Page-lock ownership is
reference-counted per OS page so allocator neighbors cannot unlock one another.
The decrypted index blob on unlock lives in `crypto::SecureBytes`; serialized
save/CommitLane snapshots use an unlocked `WipingBytes` allocator that wipes
every released capacity block. This closes the
last large plaintext-without-wipe surface after Phase 89 (decoded pixels →
`SecureBytes`). The `.osv` container is byte-identical; only the in-memory
representation changed.

## Vault write atomicity & concurrency

Append chunks → fsync → write index to inactive slot → fsync → flip `active_slot` → fsync.
(Full 3-phase detail in `mem:module/vault` index_io.*.)

**Phase 50 main-thread-tree architecture:**
- **Index tree is main-thread-only** — no tree locks, no concurrent mutations. Worker stages chunks; main thread attaches via `Vault::attach_staged`.
- **Vault has two FILE\* handles + write_mutex_:**
  - `read_fp_` (read-only) — all read paths (thumbnail decrypt, image fetch, VideoSource) to avoid contention.
  - `write_fp_` guarded by `write_mutex_` — worker appends chunks in whole-chunk holds.
  - `header_mutex_` — separate guard for slot-field mutations during commit.
- **CommitLane owns a jthread** — batches index writes (N=32 files or 2s), generation-ordered with coalescing (newest blob wins).
  Flush on queue drain, cancel, lock (auto-stop before key wipe), shutdown. Write failure halts queue (hard stop, error on status page).
  Crash mid-batch loses at most the last batch's index entries; orphans reclaimable by compact.

**Phase 65 blocking-job concurrency model:**
`MigrationJob` follows `FileOpJob`'s contract: exclusive vault ownership while `active()`, main-thread polls progress/outcome, renders a modal. **One coordinator thread** owns the index tree and all `fp_` writes (guarded by `write_mutex_`); **a decode pool** of `max(1, hardware_concurrency()-1)` workers runs decrypt → probe/sniff → encode poster in parallel. Results read through `vault::read_thumb_span` (any-thread-safe). Queue bounded at ~`workers * 2` to stay within the 256 MiB `mlock`'d budget (decoded frames + posters are `SecureBytes`). One `commit_index()` at the end, then watermark write, then `compact()` if `wasted >= AUTO_COMPACT_MIN_WASTE`. Cancel commits work but does NOT stamp watermark (re-offered next unlock). Crash leaves vault as-is; orphaned chunks are dead ciphertext reclaimed by compact. The idle auto-lock is suppressed while the job is active, and `App::shutdown()` aborts+joins it before any vault teardown (Phase 79) — the vault must never be locked/destroyed under the live coordinator.

## Other memories
- Tech stack, pinned deps, build mechanics, CI matrix: `mem:tech_stack`
- Build / run / test commands: `mem:suggested_commands`
- Code conventions (naming, error handling, headers, testing policy): `mem:conventions`
- Task-completion checklist: `mem:task_completion`
- Full `.osv` binary layout (header/chunk/index byte fields): `mem:vault_format`
- UI/UX specification (screen designs, F1 help convention): `mem:ui_spec`
- How this memory graph is meant to be structured/maintained: `mem:memory_maintenance`
