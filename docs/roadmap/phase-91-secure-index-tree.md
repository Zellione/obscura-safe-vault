# `SecureBytes` for the index tree (Phase 91)

**Status:** 🔜 ready for review
**Date:** 2026-08-27

## Problem

The break-in/hardening effort (`docs/break-in-effort.md`) measured the largest
plaintext blobs the app holds and closed the decoded-pixel gap (Phase 89). But
the **index tree** — every node name, per-node tag, tag category, tag
description, tag field value, and saved-search name — still lived in plain
`std::string` heap buffers: never page-locked, never wiped. For a 50k-item
vault that is several megabytes of plaintext *metadata* (filenames, tags,
descriptions) describing the user's library, sitting in swappable allocator
memory that was only freed — not wiped — on lock, rename, re-tag, or
`compact()`. The deferred item from the effort ("`SecureBytes` for the index
tree") was never done.

## What shipped

- **`crypto::SecureString`** (new `src/crypto/secure_string.h`) — a string type
  over an mlock'd (best-effort), `crypto_wipe`'d-on-destroy heap buffer, with
  **no SSO**: even a 4-byte name is its own locked, wiped allocation. Copyable
  (a deep copy into a fresh locked buffer) because `IndexNode` must stay
  copyable for `compact()`'s copy-rebuild-publish; movable (steal the buffer).
  Read access is explicit `.view() -> std::string_view` — no implicit
  conversion, so a name can never silently copy into a plain `std::string`.
  Provides the usual equality/ordering against `std::string_view` (so
  `node.name == std::string(...)` and `== "literal"` keep compiling), `std::hash`,
  `is_locked()`, `wipe()`, `clear()`, `resize()` + writable `data()` for
  deserialisation. Fallible `assign()` is self-view safe and preserves the old
  value on OOM; copy/construction/assignment terminate instead of silently
  manufacturing empty metadata that a later commit could persist.
- **Index tree fields converted** (`src/vault/index.h`): `IndexNode::name`,
  `IndexNode::tags`, `SavedSearch::name`, `TagCategory::name` + `fields`,
  `TagDescription::tag` + `text`, `TagFieldValue::tag` + `field` + `value` are
  all `crypto::SecureString` / `std::vector<crypto::SecureString>` now.
  `SavedSearch::query` is a copyable `crypto::SecureBlob`: although opaque to
  the vault layer, its encoded clauses contain tags, group names, and filename
  filters and therefore receive the same lock/wipe treatment.
- **Serialisation** (`src/vault/index.cpp`) reads already-wiped secure buffers:
  every deserialise path builds its strings straight into `SecureString`, and
  every host-side mutation (`set_tag_description`, `set_tag_field_value`,
  `set_category_template`, `rename/remove_template_field`, `seeded()`) copies
  into secure storage.
- **Transient index blob is no longer a plain heap vector.** The unlock path's
  decrypted index blob now decrypts straight into `crypto::SecureBytes`
  (`try_load_slot` via `open_to` + the `decode_frame` SecureBytes overload) and
  wipes on scope exit. Save paths use `crypto::WipingBytes`, whose allocator
  wipes every released capacity block (including serializer growth,
  compression replacement, CommitLane coalescing, success, and failures).
  These multi-MiB transient buffers are wiped but deliberately not mlock'd so
  they do not evict the secure tree from the finite lock budget.
- **Page-aware locking:** all secure buffer types share a page-refcount
  registry. Heap allocations can share a page and Linux memory locks do not
  stack, so a neighboring string's destructor may call `munlock` only when the
  final secure range on that page is gone. Large buffers still use one range
  lock syscall.
- **Call-site sweep** (the mechanical bulk): every consumer of the converted
  fields across `src/vault/` and `src/ui/` (including the screen files that only
  the `osv` app target compiles) reads through `.view()`, builds `std::string`
  path concatenations explicitly, and compares via the new operators. All
  ~380k `std::format`/concat/comparison call sites audited.

## Tests

- `tests/crypto/test_secure_string.cpp` (21 tests) — construction, deep copy /
  move ownership, assignment, equality against every string kind, ordering,
  hashing, in-place deserialisation (`resize` + `data`), wipe, clear, and the
  `is_locked()` invariant; plus self-view assignment, allocation-failure strong
  guarantee, overlapping-page lock lifetime, and wiping-vector reallocation.
- `index_names_and_tags_are_mlocked` (`tests/vault/test_index.cpp`) — a
  deserialised node's name and tag report `is_locked() == true`, pinning
  Phase 89's `decoded_pixels_are_mlocked`-style invariant for the whole tree.
- The full suite passes unchanged (no behavioural change expected — the on-disk
  format is byte-identical).

2168 tests / 0 failed (Debug, `--release`, `--no-av`, `--asan`, and `--tsan`
all green).

## Deliberately unchanged

- The `.osv` format and `INDEX_VERSION` stays 12 — a buffer-type change is
  invisible to the container. Serialised bytes are identical.
- The 256 MiB lockable budget and the best-effort / warn-once /
  degrade-to-swappable contract. `SecureString` draws on the same budget, but
  allocator neighbors share one page lock and retain it until the final secure
  allocation on that page is destroyed. The wipe-on-destroy guarantee holds
  regardless of lock-budget exhaustion.
- `Vault::compact()`'s copy-rebuild-publish (the copy now deep-copies secure
  strings — a transient, lockable double of the tree during the rare background
  compact).
- The transient serialisation buffer remains vector-shaped for byte appends,
  but uses `WipingAllocator`; locking a multi-MB blob during an I/O commit would
  exhaust the budget for nothing.

## Notes for reviewers

- **Unlock-time allocation cost:** deserialising a big tree performs one heap
  allocation per name/tag. Page locks are reference-counted and range-batched;
  failed locks still degrade through the existing warn-once path.
- **`mem:conventions` honoured:** no new implicit conversions from string-like
  input (explicit `SecureString(std::string_view)`); `[[nodiscard]]` on the
  failure-reporting API; fallible assignment preserves its original value, and
  infallible value operations terminate rather than silently corrupt metadata.
