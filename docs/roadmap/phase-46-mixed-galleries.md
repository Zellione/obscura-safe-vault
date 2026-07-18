## Phase 46 — Mixed galleries (images + videos + sub-galleries together) ⬜

**Goal:** Relax the standing invariant ("a gallery shows either sub-galleries
or images, never a mix") so a gallery can behave like a real filesystem
folder: any combination of images, videos, and sub-galleries as direct
children.

**Confirmed while scoping Phase 45:** the restriction is a single validation
check — `if (holds_galleries(*g)) return InvalidArg;` in `Vault::add_image`/
`add_video` (and the mirror check in `create_gallery`) — not a storage-format
limitation. `IndexNode`'s `children` vector already allows heterogeneous
`node_type` values; the `.osv` binary format needs no version bump.

### Known-shape follow-on work (not designed yet — brainstorm when picked up)
- Drop the leaf-exclusivity checks in `vault.cpp`.
- `GalleryGrid` grid layout renders folder tiles and media tiles side by
  side; needs a defined ordering rule for mixed content under each sort key
  (Phase 37) — most file managers put folders first, then media, within
  whichever sort is active.
- Phase 44's Move/Transfer/Combine dialogs currently split destinations into
  `image_target_galleries` (leaf-only) vs `gallery_target_parents`
  (subgallery-holding-only) — with the invariant gone, every gallery becomes
  a valid destination for both, finally closing Phase 44's explicitly
  deferred "generalized heterogeneous move" item.
- Cover-thumbnail selection (Phase 19) is already type-agnostic; likely
  unaffected.
- No data migration: existing vaults are already valid under the relaxed
  rule (a pure gallery is a special case of a mixed one), so old vaults just
  keep opening and working.

### Acceptance criterion
Not yet defined — pending a full design session when this phase starts.

### Out of scope (deferred)
- Everything not listed above; this entry is a roadmap placeholder only.
