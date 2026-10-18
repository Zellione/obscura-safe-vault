# Phase 120 — Export gallery folders

Status: implemented; validation and PR gates in progress.

## Behavior

Select galleries with Space (or Ctrl+A), press X, confirm the default-cancel
plaintext warning with Y, and choose a destination folder. Each selected gallery
becomes a folder containing all its nested galleries, images, and videos.
Empty galleries are preserved. Mixed selections work in the gallery grid,
favorites/tag collections, and advanced-search results. A selected descendant
already covered by a selected gallery is exported once, regardless of selection
order. The viewer's single-original export keeps the same behavior.

Original media bytes are preserved verbatim; thumbnails and vault metadata are
not exported. Existing entries are never overwritten or merged: files receive
`name (n).ext`, folders receive `name (n)`. Names are sanitized at every level.

The background job reports progress and results in items (files plus folders).
Cancel stops between entries and retains completed output. A directory-creation
failure skips and counts its entire subtree as failed, without flattening its
contents into the parent; other selected items continue. Read/write failures may
leave empty or partial files, as in the existing individual-media exporter.

## Implementation and security

- `ui/export.*` builds an iterative, deduplicated traversal of the explicit
  selection. Its descriptor stack grows with gallery depth, not gallery count.
- `platform::OutputDirectory` pins the user-chosen root once. Descendants use
  `mkdirat` and `openat(O_DIRECTORY|O_NOFOLLOW)` relative to open parents;
  output files use the existing exclusive, no-follow creation mechanism.
  Directory names are claimed with mode 0700. Symlinks and existing entries are
  suffix collisions. Display paths are informational and are never reopened.
- The existing `create_new_file_within` API delegates to the same handle-based
  mechanism. `export_one_media` decrypts into `SecureBytes`, writes only through
  the open file handle, and wipes scratch on every return path.
- Explicit per-export consent remains mandatory. Phase 120 extends the selected
  scope to selected gallery subtrees; it does not introduce an implicit whole-vault
  action. Legacy read-only and v3 vaults share the format-neutral read APIs.
- A concurrent directory rename cannot redirect subsequent writes to a replacement
  symlink. As with Phase 98, an external process can still move already-exported
  output; this is not a guarantee against relocation of plaintext by its owner.

## Acceptance and validation

- Nested images/videos match their original bytes; empty galleries survive.
- Duplicate and overlapping selections emit a single copy.
- Consent cancellation and pre-start cancellation create nothing.
- Existing files/directories/links are preserved, hostile names remain contained,
  and writes remain attached to open directories after pathname replacement.
- Directory failures account for skipped descendants; progress includes folders.
- The v3 FileOpJob worker exports a gallery subtree successfully.
- Required gates: Debug, Release, no-AV, ASAN/UBSan/LSan, CI and SonarCloud.

The initial TDD run reproduced four failing recursive-export tests (19 failed
checks) before implementation. Final gate results will be recorded before handoff.
