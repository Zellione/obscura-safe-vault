## Phase 19 — Gallery cover thumbnails ✅

**Goal:** Replace the generic folder icon on gallery tiles with a representative
**cover** derived from the gallery's contents, so the grid is browsable at a
glance.

### Tasks
- [x] `src/ui/gallery_cover.{h,cpp}` — pure, SDL-free cover resolution:
  - [x] **Leaf gallery** → the **first image's** thumbnail span (a leaf video's poster span if the first child is a video).
  - [x] **Non-leaf gallery** → an ordered list of **up to 4 sub-gallery covers** (one per sub-gallery, in child order), each resolved **recursively** (a sub-gallery's single cover = its own first-image/poster, or its first sub-gallery's cover). Depth-bounded (reuse the index depth limit, `INDEX_MAX_DEPTH`) and cycle-free.
  - [x] Returns thumb chunk spans only — selects nothing requiring a full decode; an empty subtree yields no covers (→ folder-icon fallback).
- [x] `src/ui/cover_layout.{h,cpp}` — pure montage geometry: given a tile rect and 1–4 covers, return the sub-rects (single fill for 1; 2×2 arrangement for 2/3/4, graceful for 1–3). Unit-tested.
- [x] **Grid rendering** — `GalleryGrid` draws a gallery tile as either a single cover (leaf) or the montage (non-leaf), falling back to the existing folder icon when no cover resolves. Reuses the existing thumbnail texture cache via a new free friend `vault::read_thumb_span` (kept off `Vault` to respect the cpp:S1448 method cap; keyed by thumb offset; decrypt → off-thread decode → GPU upload — **no new disk path**). Cover resolution + the per-cover texture fetch are kept as free functions so `GalleryGrid` stays within the SonarCloud method budget (cpp:S1448).
- [x] `tests/ui/` — cover selection/order/recursion/fallback (leaf, folder-of-folders, mixed depths, empty, capped at 4, depth-limit); montage geometry for 0/1/2/3/4/clamped covers; `tests/vault/` round-trips `read_thumb_span` against `read_thumbnail`. Cover resolution touches no decode and no disk (it only walks the in-memory index).

### Acceptance criterion
A leaf gallery tile shows its first image (or first video's poster); a
folder-of-folders tile shows a 2×2 montage of up to four sub-gallery covers; an
empty gallery shows the folder icon. Browsing decrypts only the small thumbnail
blobs (no full-image decode) and writes nothing to disk.
