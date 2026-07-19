## Phase 46 — Mixed galleries (images + videos + sub-galleries together) ✅

**Goal:** Relax the standing invariant ("a gallery shows either sub-galleries
or images, never a mix") so a gallery can behave like a real filesystem
folder: any combination of images, videos, and sub-galleries as direct
children.

**Confirmed while scoping Phase 45:** the restriction is a single validation
check — `if (holds_galleries(*g)) return InvalidArg;` in `Vault::add_image`/
`add_video` (and the mirror check in `create_gallery`) — not a storage-format
limitation. `IndexNode`'s `children` vector already allows heterogeneous
`node_type` values; the `.osv` binary format needs no version bump.

### Completed work
- Dropped the leaf-exclusivity checks in `vault.cpp` (three guards in
  `add_image`, `add_video`, `create_gallery`).
- `GalleryGrid` grid layout renders folder tiles and media tiles side by
  side; folders always precede media, within whichever sort key is active
  (Phase 37). Sort-order logic in `gallery_sort.h` already handles mixed
  content.
- Phase 44's Move/Transfer/Combine dialogs now accept any gallery as a
  destination for both images and sub-galleries (removed two action-gates
  and transfer/combine type-pruning). Mixed-source dispatch in `combine`
  was fixed.
- Cover-thumbnail selection (Phase 19) was already type-agnostic; unaffected.
- No data migration: existing vaults remain valid under the relaxed rule
  (a pure gallery is a special case of a mixed one), so old vaults just
  keep opening and working.

### Acceptance criterion
- `scripts/test.sh` green (924 tests), incl. new mixed-gallery vault/transfer/combine tests.
- `scripts/test.sh --asan` clean (no memory/UB errors).
- Existing vaults open unchanged (no format change/migration required).
- Manual verification: a gallery holding an image + a sub-gallery renders both
  (folders-first); image viewer navigates media only; import-files and
  create-sub-gallery actions available everywhere; plain `.zip` import prompts
  for a new sub-gallery name.
