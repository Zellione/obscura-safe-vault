## Phase 13 — Favorites ✅

**Goal:** Mark images or galleries as *favorite* and browse them through two
dedicated screens — an **Image Favorites** section and a **Gallery Favorites**
section.

### Tasks
- [x] **Index format extension** — a dedicated `favorite` `u8` flag on both gallery and image nodes (bump `INDEX_VERSION` again; pre-existing vaults read as not-favorited). A dedicated flag, *not* a reserved tag, keeps favorites out of the tag namespace and out of tag search.
- [x] `Vault` API — `toggle_favorite(node_path)`; `list_favorite_images()` (flat, whole-tree) and `list_favorite_galleries()`; persisted via the crash-safe index swap.
- [x] **Toggle UX** — a single key marks/unmarks the focused image or gallery (`B` for bookmark — `F`/`L`/`T` are already bound in the viewer); favorited tiles show a small gold star badge in the grid (and a gold bar in the list view).
- [x] **Two distinct screens** — `src/ui/favorites_images.{h,cpp}` (a flat grid of every favorited image across the vault, opens the viewer on activate) and `src/ui/favorites_galleries.{h,cpp}` (a list/grid of favorited galleries; activating one navigates to that gallery in the normal grid). Both reachable via keys from the gallery grid (`F` images, `Shift+F` galleries).
- [x] Update `CLAUDE.md` module layout + `mem:core`.
- [x] `tests/` — favorite flag round-trip for images and galleries; favoriting images populates the image-favorites list across the tree; favoriting a gallery populates the gallery-favorites list; un-favorite removes from both; a pre-favorites vault opens with none favorited.

### Acceptance criterion
Favoriting images and galleries populates the two distinct favorites screens;
the flags survive a lock/reopen; opening a favorite gallery navigates to it and
opening a favorite image opens the viewer.

**Status:** ✅ 273/273 tests pass under `scripts/test.sh` and `--asan` (8 new
vault favorites tests + 5 new index favorite round-trip/back-compat tests). The
index format bumped to `INDEX_VERSION = 3` and reads v1/v2 blobs back-compatibly
with `favorite = false`; the fuzz corpus now seeds favorited gallery + image
nodes. The favorite flag lives on `IndexNode` (gallery + image); the vault layer
adds `toggle_favorite` + flat whole-tree `list_favorite_images`/
`list_favorite_galleries` (persisted via the crash-safe double-buffer swap). The
two favorites screens are first-class `Screen`s (`NavKind::ToFavoriteImages`/
`ToFavoriteGalleries`); `B` toggles the focused tile in the grid and the current
image in the viewer.

> **Notes / decisions made during implementation**
> - **Favorite byte placement.** The `favorite u8` is written immediately after the
>   tag block, uniform for both node kinds, so the reader parses it version-gated
>   (`version >= 3`) without branching on node type — mirroring the Phase 12 tag block.
> - **Favorites lists reuse `SearchHit`.** `list_favorite_*` return `SearchHit`
>   (path + node + kind) for consistency with `search`, leaving `effective_tags`
>   empty (favorites don't compute the tag cascade).
> - **Toggle doesn't disturb selection.** In the grid, `B` flips the flag on the
>   same in-memory node the tile points at and repaints on the input event — no
>   `refresh()`, so the export multi-selection is preserved.
> - **Favorites images open a favorites-scoped viewer.** Activating a favorited
>   image opens the viewer over the *whole* favorites set (`NavKind::ToFavoriteViewer`),
>   so `<-`/`->`, the strip, and the slideshow iterate the favorites rather than one
>   gallery; `Esc`/`Up` returns to the favorites grid. The viewer gained a "collection
>   mode" — an explicit image set with a per-image full path and an exit target — so it
>   is no longer tied to a single `gallery_path_`. Favorited galleries navigate the
>   normal grid.
