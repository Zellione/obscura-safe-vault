# Module: vault/ + crypto/ — storage & security core

Referenced from `mem:core`. Covers `src/crypto/` (Monocypher wrappers) and `src/vault/`
(the `.osv` container format). Galleries may hold any mix of images, videos, and
sub-galleries — no leaf-only restriction (the old insertion guards were removed).

## crypto/
- `aead.*`, `kdf.*`, `random.*`, `secure_mem.h`, `crypto.h` — Monocypher wrappers:
  XChaCha20-Poly1305 AEAD, Argon2id KDF, platform CSPRNG (getrandom/BCryptGenRandom),
  `SecureBytes`/mlock buffers, `crypto_wipe`. macOS unsupported (`#error` guard in
  `random.cpp`).

## vault/ — `.osv` container
Core files: `vault.*`, `header.*`, `index.*`, `chunk_store.*`, `byte_io.h`, `file_util.h`.

### Phase 50 concurrency: "main-thread tree" architecture
The index tree is **main-thread-only**; no tree locks exist. The vault file opens two handles + one write-path mutex for thread-safe import background queue:
- **`read_fp_`** — second read-only unbuffered FILE* (opened at unlock, closed+wiped at lock). All read paths move to it: thumbnail decrypt, full-image fetch, `VideoSource` (chunks are immutable once appended, so reads never race worker appends). No contention with worker writes.
- **`write_fp_` + `write_mutex_`** — original write handle guarded by a std::mutex. Worker appends chunks under lock (whole chunks, no bounded slices to avoid interleaving hazards).
- **`header_mutex_`** — separate guard for slot-field mutations during commit (active_slot flip + generation update), to reduce lock hold time contention.
- **`Vault::lock()` auto-stops the CommitLane** before key wipe, preventing mid-flight commits after the key is gone.

### staging.* (Phase 50) — worker-to-tree hand-off
- `stage_image(Vault, data, ...)` / `stage_video(data, ...)` — any thread, stream encrypted chunks to disk with fflush (no fsync), return a ready IndexNode with chunk spans. Plaintext stays mlock'd; nodes are ready but **not attached**.
- `attach_staged(Vault, node, gallery_path)` — main-thread only, performs tree insertion, **no commit issued**. Commit is scheduled separately by batching policy (see CommitLane below).
- `ensure_gallery_path(Vault, path)` — creates missing ancestor galleries as needed on attach.

### safe_name.* — node-name rules (a node name is a single path COMPONENT, never a path)
- `is_safe_node_name` = REJECT. Vault ingress trust boundary: `add_image`/`add_video`/
  `create_gallery`.
- `sanitize_node_name` = REPAIR. Importers (zip_plan basename/dir components, meta_json
  titles, file_op_job picked files) — an awkward archive name must not fail a whole import.
  Output ALWAYS satisfies `is_safe_node_name` (property-tested).
- Rejects `/` and `\` on every platform, `.`/`..`, NUL/control/DEL, Windows-reserved
  chars + device names (CON/NUL/COM1-9/LPT1-9), trailing dot/space, >255 bytes (truncated
  on a UTF-8 codepoint boundary). Bytes >=0x80 stay opaque (CJK).
- WHY: a `.osv` is UNTRUSTED INPUT (portable/shareable). `index.cpp` reads `name` as opaque
  bytes; `ui::export_*` turns it back into a real path. `dest_dir / name` does NOT contain
  — an ABSOLUTE name discards `dest_dir` (CWE-22). Sink-side guard `ui::export_path_within`
  (weakly_canonical + lexically_relative, fails closed) is required because vaults already
  on disk can carry hostile names — ingress validation alone is not enough.

### Free friends (kept off Vault to stay under the cpp:S1448 35-method cap)
- `vault::read_thumb_span(v,offset,length,out)` — decrypt a thumb/poster chunk by raw span
  (gallery cover montages); InvalidArg if len 0, Locked, AuthFailed on tamper.
- `vault::gallery_sort_key(v,path)` / `vault::set_gallery_sort(v,path,SortKey)` — persisted
  via commit_index. `Vault::list` resolves the target gallery's stored sort_key against the
  vault-wide default through `ui::effective_sort_key` before returning, so every caller (grid,
  list view, viewer strip, slideshow) gets one order for free. Pure ordering logic lives in
  `ui/gallery_sort.*` (see `mem:module/ui`).
- `vault::vault_settings(v)` / `vault::set_vault_settings(v,VaultSettings)` (Phase 49) —
  vault-global settings; the setter persists through the same crash-safe commit_index swap and
  returns `Locked` on a locked vault. Held in `Vault::settings_`; `reset()` clears it and
  `create()` seeds it. **Both move operations must carry `settings_`** — they originally
  omitted it, silently dropping settings on a move (fixed, regression-tested).
- `vault::rename_node(v,gallery_path,old_name,new_name)` — validates `is_safe_node_name` +
  no sibling collision, then a pure leaf-field edit (an IndexNode persists only its local
  name, never a path, so no cascade). Drives the `R` RenameDialog.
- `vault::repair_video_metadata` — re-probes a node stuck at placeholder Unknown video
  metadata and fills it in if the codec has since become decodable; no-op otherwise.
  Test-only friend `vault::test_only_force_video_codec_unknown` (in tests/vault/test_video.cpp).

### index.* — the index tree
- `IndexNode` carries `std::vector<std::string> tags` + `bool favorite` (gallery + image),
  a `SortKey` u8 (meaningful only on Gallery nodes: Default/NameAsc/NameDesc/DateAsc/DateDesc/
  SizeAsc/SizeDesc/Insertion; out-of-range byte rejected, not clamped, bounded PER VERSION —
  v6/v7 max 6, v8 max 7), and Type::Video + VideoMeta (multi-chunk list + poster).
- `INDEX_VERSION=8`. Vault-global SavedSearch block after the root (name + opaque
  `ui::AdvancedQuery` blob, `INDEX_MAX_SAVED_SEARCHES=4096`), then the Phase 49 vault-global
  **settings block** — see `mem:vault_format` for its byte layout. `INDEX_MAX_TAGS=4096`.
  Back-compat: v1–v5 read as empty tags / favorite=false / no saved searches; pre-v8 read with
  `default_sort=Insertion`, tile tags on, and `VaultSettings::seeded()`.
- Phase 49 types: `TagCategory{std::string name; uint8_t swatch;}` and
  `VaultSettings{SortKey default_sort; bool tiles_show_tags; std::vector<TagCategory>
  categories;}` with a static `seeded()` (8 nhentai-style categories on distinct swatches).
  Caps `INDEX_MAX_TAG_CATEGORIES=256`, `INDEX_MAX_CATEGORY_BYTES=64`, `TAG_SWATCH_COUNT=16`.
  `write_settings`/`read_settings` mirror the saved-searches pair — the writer CLAMPS, the
  reader REJECTS. `serialize_index`/`deserialize_index` gained 4-arg forms; the 2- and 3-arg
  ones delegate. **Any new call site must use the 4-arg form** — the fuzz harness's base blob
  does, so category bytes are reachable by mutation.
- Favorites: `Vault::toggle_favorite(node_path)` + flat whole-tree
  `list_favorite_images()`/`list_favorite_galleries()` -> `vector<SearchHit>`.
- Tag API + scoped search: `set_tags`/`add_tag`/`remove_tag(node_path)`,
  `search(query, SearchScope{Images,Galleries,Both})` -> `vector<SearchHit>`.
  Read-time tag cascade (effective tags = own ∪ ancestor galleries; root tags global).
  `resolve_node` resolves a path to a gallery OR image.

### vault_search.* — VaultSearch facade (friend over Vault&, keeps Vault under S1448 cap)
- `all_tags()` (distinct case-insensitive vocab), `run_search(ui::AdvancedQuery)` ->
  `vector<SearchHit>` ranked by score then path.
- `save_search`/`list_saved_searches`/`delete_saved_search` (upsert by name, persisted via
  commit_index).
- `tag_overview()` -> `vector<ui::TagTally>` (per-distinct-tag direct {gallery,image} counts,
  no cascade), `galleries_with_tag()` / `images_with_tag()` (direct carriers only, no cascade).

### transfer.* — cross/same-vault move & copy
- `transfer_image(src,src_gallery,file,dst,dst_gallery,mode)`: read→add_image→(Move?
  remove_image). Dst commits before src, so a crash = duplicate, not loss; plaintext stays
  in mlock'd SecureBytes. `image_target_galleries(v)` lists leaf paths that can hold images.
- `transfer_gallery(src,src_gallery,dst,dst_parent,mode)`: recursive copy-then-(Move? delete);
  `remove_gallery(src)` runs LAST for Move (crash = dup). `gallery_target_parents(v)`.
- `enum TransferMode{Move,Copy}` (Copy leaves source). Same-vault (`&src==&dst`) supported;
  `transfer_gallery` rejects a `dst_parent` == or inside the src subtree (cycle). Pure over
  the public Vault API.
- `transfer_images` (bulk list driver, `TransferTally{done,failed}`). Optional
  `vault::OpProgress*` (`op_progress.h`; total/done + cooperative cancel) on
  transfer_images/transfer_gallery/export_images — a cancelled gallery Move leaves the source
  intact. `ui::ImportProgress` aliases OpProgress.
- `transfer_galleries(src,src_paths,dst,dst_parent,mode,progress)` — bulk gallery move/copy,
  loops `transfer_gallery` per path WITHOUT forwarding progress into it (avoids clobbering the
  bulk total), bumping `progress->done` per item. Feeds `FileOpJob::start_transfer_galleries`
  + GalleryGrid mass-move (homogeneous-selection dispatch — images-only / galleries-only /
  mixed = error).

### combine.* — recursive gallery-merge engine
- `combine_galleries(src,src_gallery,dst,dst_gallery,tally,progress)` merges src into dst
  (same- or cross-vault): media leaves -> `transfer_image` per file (collisions skipped +
  tallied); sub-gallery leaves -> recurse if a same-named dest child exists, else
  `transfer_gallery` the whole subtree wholesale (Move); tags unioned case-insensitively via
  `add_tag`; source gallery removed once fully empty.
- Rejects type mismatches and same-vault cycles (dest == src or nested inside it — the
  reverse, src inside dest, is a legal "flatten upward").
- `combine_target_galleries(dst,src,src_gallery)` lists eligible destinations.
  `CombineTally{media_moved,media_skipped,galleries_merged,galleries_moved}`.

### Internal components (extracted from Vault to keep it under the S1448 cap)
- `index_io.*` (Phase 50 split) — index serialisation + crash-safe double-buffer slot swap (append → write
  inactive slot → flip active_slot; 3-phase atomic commit). `IndexIoContext` bundles mutable
  state. **Split into**:
  - `serialize_plain_index(vault, context)` — memory-only, fast, produces a serialized index blob.
  - `commit_plain_blob(vault, blob, generation)` — enqueues the blob to CommitLane with a generation tag for ordered, coalesced writes.
- `vault_ops.*` — tree navigation + path resolution + structural validation (split_path,
  resolve_gallery, resolve_node_impl, child_named, holds_media, holds_galleries,
  for_each_media, relocate_node_chunks). Pure traversal, no I/O. `push_child(children,node)`
  wraps the `vector::push_back` in try/catch (alloc failure → IoError, not terminate());
  `push_child_fail_after` mirrors `resize_fail_after` fault-injection for tests.
### commit_lane.* (Phase 50) — batched, ordered index commits
- `CommitLane` owns a jthread + a CV-based work queue. Runs independently, stops gracefully on Vault::lock().
- `enqueue(generation, blob)` → appends if not stopped; dequeues stale blobs (coalescing).
- Main thread serializes index every N=32 files or 2s, tags it with generation, hands to lane.
- Lane writes the inactive slot, fsyncs, flips active_slot in generation order — newest blob always wins.
- Write failure is a hard stop: queue halts, error surfaces on status page; already-committed work safe.
- Commit-lane flush runs on queue drain, cancel, lock, and shutdown (ordered final write before key wipe).

- `chunk_codec.*` — pure adaptive store-if-smaller deflate framing: method byte (0=raw,
  1=deflate) + bounded `orig_len` inside the AEAD; used by ChunkStore's framed ctor flag
  (← header `FLAG_FRAMED_CHUNKS` bit) + the index-blob sites. miniz tdefl/tinfl, no new dep.
  The `resize_buf` overload wraps `resize()` in try/catch (it is noexcept; an uncaught
  alloc-failure exception there would terminate() the process); `resize_fail_after` fault
  injection makes the failure path deterministically testable. Legacy vaults (header flag
  unset) read AND append raw forever — no migration.
