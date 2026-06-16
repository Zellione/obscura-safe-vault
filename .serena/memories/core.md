# obscura-safe-vault — Core

Multi-platform encrypted photo gallery. Single `.osv` vault file. Decrypted data lives only in `mlock`'d heap — never written to disk.

## Source map

```
src/
  app/         main.cpp, app.{h,cpp}          — state machine + SDL event loop.
                                               Event-driven: blocks on
                                               SDL_WaitEventTimeout when idle and
                                               renders only on input / async result /
                                               animation (Screen::animating() +
                                               mark_dirty/consume_dirty), so the GPU
                                               idles instead of free-running. VSync on
                                               (Window::vsync()); manual ~60fps cap
                                               fallback when VSync is unavailable.
  crypto/      aead.*, kdf.*, random.*,        — Monocypher wrappers
               secure_mem.h, crypto.h
  vault/       vault.*, header.*, index.*,     — .osv container format
               chunk_store.*, byte_io.h, file_util.h
                                               — index.h: IndexNode carries
                                                 std::vector<std::string> tags +
                                                 bool favorite (gallery + image);
                                                 INDEX_VERSION=3 (v1/v2 read back-
                                                 compat: empty tags, favorite=false);
                                                 INDEX_MAX_TAGS=4096. Favorites
                                                 (Phase 13): Vault::toggle_favorite
                                                 (node_path) + flat whole-tree
                                                 list_favorite_images()/
                                                 list_favorite_galleries()->vector
                                                 <SearchHit>. Vault has tag
                                                 API + scoped search (Phase 12):
                                                 set_tags/add_tag/remove_tag(node_path),
                                                 search(query, SearchScope{Images,
                                                 Galleries,Both})->vector<SearchHit>;
                                                 read-time cascade (effective tags =
                                                 own ∪ ancestor galleries; root tags
                                                 global); resolve_node resolves a path
                                                 to a gallery OR image.
  image/       decode.*, thumbnail.*,          — stb_image decode, thumb gen
               format_registry.*,              — magic-byte format detection
               decoder.*,                      — Decoder interface + DecoderRegistry
                                                 (polymorphic dispatch; default_registry()
                                                 wires WebP/HEIF/stb decoders)
               decode_webp.*, decode_heif.*    — libwebp (WebP), libheif (HEIC/AVIF)
               decode_worker.*                 — off-thread image decoder: caller
                                                 reads+decrypts on its thread, worker
                                                 runs decode_from_memory() on one bg
                                                 thread, caller uploads result to GPU.
                                                 Coalesces by key, SDL wake event,
                                                 retain()/pending() (each screen owns
                                                 its own worker; FullTexCache +
                                                 GalleryGrid use it for async decode)
  gfx/         window.*, renderer.*,           — SDL3 window/renderer, texture cache,
               texture_cache.*, text.*,        — stb_truetype text atlas
               theme.h                         — "Refined Slate" colour tokens +
                                                 RADIUS consts; renderer has
                                                 draw_round_rect / draw_selection_glow
                                                 (round_rect_outline is pure/tested).
                                                 Window::width()/height() are LIVE
                                                 (SDL_GetCurrentRenderOutputSize, px) so
                                                 layout reflows on resize. Font baked at
                                                 28px; draw_text y = top, baseline=y+px;
                                                 use FontAtlas::text_top_for_center to
                                                 vertically centre text in a box.
                                                 draw_text batches a run into ONE
                                                 SDL_RenderGeometry call (per-vertex
                                                 colour) via build_text_geometry
                                                 (pure/tested); draw_round_rect reuses
                                                 scratch buffers + a cached arc table.
  ui/          unlock_screen.*, gallery_grid.* — UI screens; gallery has Grid +
                                                 detailed List views (key L), live
                                                 width reflow, centred/elided labels
               image_viewer.*, widgets.*       — viewer has Fit + FillScroll + Slideshow
                                                 modes, bottom/left strip toggle (keys
                                                 F/T, P starts slideshow); widgets has
                                                 button_state + elide_middle
               full_tex_cache.*                — shared decode→GPU full-res texture cache
                                                 (decrypt into mlock'd SecureBytes, wipe
                                                 after upload); used by viewer + slideshow
               slideshow_view.*                — full-screen slideshow SDL plumbing
                                                 (owns SlideshowModel + cross-fade render);
                                                 Phase 11
               strip_layout.*                  — orientation-aware strip geometry +
                                                 half-size thumbnails (pure/tested)
               scroll_model.*                  — fill-width continuous scroll maths
                                                 (pure/tested)
               slideshow_model.*               — slideshow auto-advance/wrap/shuffle/
                                                 cross-fade maths (Phase 11, pure/tested;
                                                 driven by ImageViewer's Slideshow view
                                                 mode via update(dt))
               meta_format.*                   — list-view metadata formatting:
                                                 size/dimensions/date/type (pure/tested)
               selection_model.*               — multi-select state for export
                                                 (Phase 10, pure/tested)
               consent_dialog.*                — modal "export anyway?" confirm
                                                 (Phase 10, SDL plumbing)
               export_ui.*                     — shared consent+folder-pick plumbing
                                                 used by gallery + viewer (Phase 10)
               export.*                        — decrypt→write-verbatim→wipe export
                                                 (Phase 10; the ONE gated deviation
                                                 from invariant #1, SDL-free/tested)
               search_model.*                  — pure query tokenise/match/rank
                                                 (Phase 12, pure/tested); used by the
                                                 search overlay for live filter+rank
               search_overlay.*                — `/` live-filtered search modal in
                                                 GalleryGrid; Tab cycles scope
                                                 (Both/Images/Galleries) (Phase 12)
               tag_editor.*                    — `G` add/remove-tags modal in both
                                                 GalleryGrid + ImageViewer (Phase 12)
               favorites_images.*              — flat grid of favorited images across
                                                 the vault; opens viewer in home gallery
                                                 (Phase 13; F from gallery grid)
               favorites_galleries.*           — flat grid of favorited galleries;
                                                 navigates the normal grid (Phase 13;
                                                 Shift+F). `B` toggles favorite on the
                                                 focused grid tile / current viewer image;
                                                 gold star badge on favorited tiles
               input.*, nav_model.*, viewer_model.h
               passphrase.*, screen.h
               secure_text_field.*, unlock_logic.*
  platform/    paths.*, file_dialog.*,         — config dirs, SDL file dialogs
               folder_dialog.*                 — export destination picker (Phase 10)
tests/
  crypto/ gfx/ image/ platform/ ui/ vault/
  test_framework.h  test_main.cpp
vendor/
  SDL3/         git submodule, built by setup.sh (cmake)
  monocypher/   git submodule, single .c compiled by premake
  stb/          git submodule, header-only
  libwebp/ libde265/ libaom/ libheif/   image codecs (Phase 9), cmake-built static
  codecs-prefix/   staging install prefix for the four codecs (gitignored)
assets/fonts/   bundled OFL font for stb_truetype
```

## Project-wide invariants (NEVER violate)

1. No decrypted bytes to disk — only `mlock`'d heap buffers.
   EXCEPTION (documented, gated): `src/ui/export.*` deliberately writes decrypted
   originals to disk on explicit user consent (selection-only, never thumbnails,
   buffer wiped right after write). No other path may write plaintext.
2. All key/KEK/password buffers wiped with `crypto_wipe` before free.
3. Every XChaCha20-Poly1305 encrypt call uses a fresh 24-byte CSPRNG nonce.
4. Tag verified before any plaintext bytes are consumed.
5. Keys, passwords, decrypted content must never appear in log output.

## Key hierarchy

`KEK = Argon2id(password [‖ keyfile], salt)` → unwraps random 32-byte master key.
All data/thumbnail/index chunks encrypted with master key + fresh nonce per chunk.

## Vault write atomicity

Append chunks → fsync → write index to inactive slot → fsync → flip `active_slot` → fsync.

## Tech stack details: `mem:tech_stack`
## Build/run/test commands: `mem:suggested_commands`
## Code conventions: `mem:conventions`
## Task-completion checklist: `mem:task_completion`
