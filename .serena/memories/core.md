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
                                               Phase 14: VaultManager is the home
                                               screen; App owns ONE unlocked vault
                                               (active_) at a time + a transient
                                               pending_ during unlock. Single-active /
                                               lock-on-switch: opening another vault
                                               wipes the old key; shutdown wipes both.
                                               promote_pending() runs only on unlock
                                               success (ToGallery while state==Locked).
                                               PR5: idle auto-lock (app/idle_timer.h,
                                               IdleTimer; reset on user input in
                                               dispatch_event; maybe_auto_lock(dt) wipes
                                               active_ + to_manager() after IDLE_LOCK_SECS
                                               = 5 min).
  crypto/      aead.*, kdf.*, random.*,        — Monocypher wrappers
               secure_mem.h, crypto.h
  vault/       vault.*, header.*, index.*,     — .osv container format
               chunk_store.*, byte_io.h, file_util.h
                                               — vault::read_thumb_span(v,offset,length,out): FREE
                                                 friend (not a member, keeps Vault under the
                                                 cpp:S1448 35-method cap) — decrypt a thumb/poster
                                                 chunk by raw span (gallery cover montages, Phase 19);
                                                 InvalidArg if len 0, Locked, AuthFailed on tamper.
               transfer.*                      — transfer_image(src,src_gallery,file,dst,
                                                 dst_gallery,mode): read→add_image→(Move?
                                                 remove_image) (dst commits before src; crash
                                                 = dup, not loss; plaintext in mlock'd
                                                 SecureBytes) + image_target_galleries(v)
                                                 (leaf paths that can hold images, incl.
                                                 eligible root).
                                                 PR3: transfer_gallery(src,src_gallery,dst,
                                                 dst_parent,mode) recursive copy-then-(Move?
                                                 delete) (snapshot subtree → recreate + copy
                                                 images → Vault::remove_gallery(src) LAST for
                                                 Move; crash = dup) + gallery_target_parents(v)
                                                 (image-free galleries that can hold a
                                                 sub-gallery).
                                                 PR4: enum TransferMode{Move,Copy} (Copy
                                                 leaves source) + same-vault transfers
                                                 (&src==&dst; transfer_gallery rejects a
                                                 dst_parent == or inside src subtree = cycle).
                                                 Pure over public Vault API. Vault::
                                                 remove_gallery drops a subtree, orphaning its
                                                 chunks (reclaimed by compaction) (Phase 14).
                                               — index.h: IndexNode carries
                                                 std::vector<std::string> tags +
                                                 bool favorite (gallery + image);
                                                 INDEX_VERSION=5: Type::Video + VideoMeta
                                                 (multi-chunk list + poster) (Phase 15) +
                                                 vault-global SavedSearch block after the root
                                                 (name + opaque ui::AdvancedQuery blob, Phase 18,
                                                 INDEX_MAX_SAVED_SEARCHES=4096);
                                                 (v1–v4 read back-
                                                 compat: empty tags, favorite=false, no saved
                                                 searches);
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
                                                 Phase 18 advanced search lives on the
                                                 vault_search.* VaultSearch facade (friend over a
                                                 Vault&, keeps Vault under the cpp:S1448 method
                                                 cap): all_tags() (distinct case-insensitive
                                                 vocab), run_search(ui::AdvancedQuery)->vector
                                                 <SearchHit> ranked by score then path,
                                                 save_search/list_saved_searches/delete_saved_search
                                                 (upsert by name, persisted via commit_index);
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
  media/       video_source.*, chunk_avio.*,  — Phase 15–16 video & audio (all gated OSV_VENDORED_AV):
               mem_avio.*, video_decoder.*,      VideoSource = decrypt-on-demand byte stream over
               audio_decoder.*, av_sync.*,       a video's ChunkStore (mlock'd 1-chunk cache);
               audio_frame.h,                    ChunkAvio/MemAvio = AVIOContext (read+seek, never
               video_probe.*, decoded_frame.h    a temp file); VideoDecoder = FFmpeg shared demuxer
                                                 feeding both video + audio via per-stream packet
                                                 queues (vq_/aq_); H.264/HEVC decode → DecodedFrame
                                                 (yuv420p/nv12, swscale fallback) + keyframe seek;
                                                 has_audio() / audio_info() / next_audio_frame()
                                                 API (Phase 16); AudioDecoder owns an AVStream*,
                                                 decodes planar PCM → interleaved F32 in AudioFrame
                                                 {samples, channels, sample_rate, pts_seconds};
                                                 av_sync = PURE logic (no SDL/FFmpeg) for audio-
                                                 clock tracking: decide(audio_clock, frame_pts, ...)
                                                 → FrameAction{Present,Hold,Drop}; audio_clock(base,
                                                 samples_consumed, rate) computes current; clamp_volume/
                                                 effective_gain helpers; unit-tested. probe_video =
                                                 container/codec/dims/duration + first-frame poster.
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
                                                 Phase 15: YuvTexture (streaming I420/NV12
                                                 video texture) + Renderer::draw_triangle
                                                 (play badge / play-pause icon).
  ui/          unlock_screen.*, gallery_grid.* — UI screens; gallery has Grid +
                                                 detailed List views (key L), live
                                                 width reflow, centred/elided labels
               image_viewer.*, widgets.*       — viewer has Fit + FillScroll + Slideshow
                                                 modes, bottom/left strip toggle (keys
                                                 F/T, P starts slideshow); widgets has
                                                 button_state + elide_middle. ImageViewer hosts
                                                 a fit-only VideoPlayback when the current item
                                                 is_video() (Phase 15: Space play/pause, J/L
                                                 +-5s, ,/. frame-step, drag seek bar).
                                                 Phase 16: M mute, [/] volume ∓5%; seek bar
                                                 seeks both video + audio tracks in-sync.
                                                 GalleryGrid routes video imports to add_video +
                                                 draws a play badge (draw_tile_thumb) in grid & list.
               playback_model.*                — pure video transport maths: clock/clamp/
                                                 seek-bar map/mm:ss/frame-due (Phase 15,
                                                 pure/tested)
               video_playback.*                — in-viewer video player: VideoDecoder + YUV
                                                 texture + SDL_AudioStream (master audio clock)
                                                 + seek bar (both tracks); mute/volume via
                                                 SDL_SetAudioStreamGain; A/V sync via av_sync::decide;
                                                 pause pauses both; pImpl gated on OSV_VENDORED_AV
                                                 (non-AV build -> poster) (Phase 15–16)
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
               advanced_search_model.*         — pure, SDL/vault-free advanced query (Phase 18,
                                                 unit-tested): AdvancedQuery{weighted include (OR
                                                 gate + scorers), exclude (hard filter), AND/OR
                                                 TagGroups + top-level join, name substring,
                                                 SearchScope}; evaluate()->{matched,score};
                                                 serialize_query/deserialize_query (opaque blob);
                                                 tag_suggestions(prefix,vocab) ranked autocomplete.
                                                 vault.cpp includes it (one-way dep) for run_search.
               advanced_search_screen.*        — `Shift+/` (NavKind::ToAdvancedSearch) dedicated
                                                 screen (Phase 18): keyboard query builder (Tab
                                                 cycles fields, autocomplete dropdown) + live
                                                 result list + saved-searches sidebar (Ctrl+S save,
                                                 Enter load/open result, Del delete). Image result
                                                 -> gallery viewer; gallery result -> ToGallery.
                                                 Coexists with the Phase 12 `/` overlay. Phase 20:
                                                 Ctrl+L toggles the result panel List <-> thumbnail
                                                 Grid (session-scoped ui::ResultView); takes a
                                                 TextureCache& + owns its own DecodeWorker (update()
                                                 pumps it); render_result_grid free friend reuses
                                                 the shared tile_thumb draw; handle_results_key uses
                                                 result_move for grid nav (Left/Right + row stride).
                                                 Query/params/cursor/view persist across visits via a
                                                 session-scoped ui::AdvancedSearchState (advanced_search_
                                                 state.h) App owns + resets on vault change
                                                 (promote_pending); restored on_enter / saved on_exit;
                                                 results re-derived (node ptrs not persisted). Ctrl+R
                                                 clears the query behind a Y/N confirm modal.
               favorites_images.*              — flat grid of favorited images across
                                                 the vault; opens a favorites-scoped
                                                 viewer (ToFavoriteViewer: prev/next
                                                 iterate the favorites set, Esc returns
                                                 to the grid) (Phase 13; F from gallery
                                                 grid). favorites_screen.* is the shared
                                                 base (grid/selection/badge) for both
                                                 favorites screens.
                                                 ImageViewer has a "collection mode"
                                                 (explicit image set + per-image path +
                                                 exit Nav) so it isn't tied to one gallery.
               favorites_galleries.*           — flat grid of favorited galleries;
                                                 navigates the normal grid (Phase 13;
                                                 Shift+F). `B` toggles favorite on the
                                                 focused grid tile / current viewer image;
                                                 gold star badge on favorited tiles
               vault_manager.*                 — multi-vault home screen (Phase 14):
                                                 lists known vaults from VaultRegistry;
                                                 open/create(save dialog)/remove/lock/
                                                 select. Emits NavKind::ToVaultManager /
                                                 LockActive / ToUnlock(path) / ToGallery.
               transfer_dialog.*               — `M` modal in GalleryGrid (Phase 14 PR2/3/4):
                                                 move OR copy selected images / (PR3) a focused
                                                 gallery subtree to another vault — or (PR4)
                                                 within the active vault. Source enum
                                                 {Images,Gallery}; open()/open_gallery().
                                                 Stages: Mode(Move/Copy) → pick dest vault
                                                 (\"This vault\" row + registry minus active;
                                                 \"This vault\" skips unlock, dest_is_self_,
                                                 dest_vault()=src_) → unlock transiently (owns
                                                 Dest{vault, pw in SecureTextField, optional
                                                 keyfile}) → pick leaf gallery (images) / parent
                                                 gallery (gallery) / new → vault::transfer_image
                                                 each or transfer_gallery once (mode_) → re-lock
                                                 the dest on every exit (~Vault backstop; src_
                                                 never locked here). Grid skips its import dlg_
                                                 poll while transfer_.active(); M with no
                                                 selection acts on the focused tile.
               quick_switch.*                  — global `` ` `` (grave) overlay (Phase 14 PR5):
                                                 lists registry vaults; choosing one emits
                                                 NavKind::ToUnlock(path) (App locks current +
                                                 unlocks chosen); Esc or the active vault =
                                                 no-op close. Hosted by GalleryGrid,
                                                 ImageViewer, and the FavoritesScreen base —
                                                 those now take a VaultRegistry& + active
                                                 vault path. consume_choice() drains the pick
                                                 (mirrors TransferDialog::consume_completed).
               zip_plan.*                      — pure ZIP placement planner (Phase 17):
                                                 archive entries -> galleries to create +
                                                 file placements + mixed-folder conflicts +
                                                 skip count. SDL-/miniz-/vault-free, unit-tested.
                                                 ZipDest{NewGallery,Append}, ZipConflictPolicy
                                                 {AskUser,FlattenMixed,SkipMixed}.
               zip_import.*                    — ZIP import executor (Phase 17): miniz reader
                                                 -> mlock'd SecureBytes (one entry at a time, no
                                                 temp file) -> Vault::add_image/add_video chosen
                                                 by image::detect_format. needs_resolution for
                                                 mixed folders. Lives in ui/ like export.* (deps
                                                 vault + image). Hosted by GalleryGrid (Z key).
               delete_summary.*                — pure recursive tally of a gallery subtree
                                                 (images/videos/sub-galleries) for the Del
                                                 delete-confirm popup (Phase 17 follow-up).
                                                 SDL-/vault-free count + plural-aware format,
                                                 unit-tested. GalleryGrid's Del removes the
                                                 focused image/video (Vault::remove_image) or
                                                 gallery subtree (Vault::remove_gallery) behind
                                                 a confirm modal showing the tally.
               gallery_cover.*                 — pure, SDL/vault-free cover resolution (Phase 19):
                                                 walks the index tree -> thumb chunk spans only.
                                                 resolve_single_cover (leaf: first image thumb /
                                                 first video poster; non-leaf: recurse first
                                                 sub-gallery) + resolve_covers (leaf: 0–1 cover;
                                                 non-leaf: up to 4 sub-gallery covers in child
                                                 order, skipping empties). Depth-bounded by
                                                 INDEX_MAX_DEPTH, cycle-free, unit-tested. No
                                                 decode, no disk.
               cover_layout.*                  — pure montage geometry (Phase 19): cover_montage_rects
                                                 (tile rect + 1–4 covers -> sub-rects; single fill
                                                 for 1, row-major 2×2 grid for 2–4). GalleryGrid's
                                                 cover montage is drawn by tile_thumb's tile_cover_tex()
                                                 + vault::read_thumb_span (offset,length) (Phase 20
                                                 extract), reusing the thumbnail texture cache /
                                                 DecodeWorker; folder icon when no cover resolves.
               result_grid.*                   — pure, SDL-free result-view state (Phase 20):
                                                 ResultView{List,Grid} + toggle_result_view +
                                                 result_move_delta/result_move (List ±1 row; Grid
                                                 ±1 / ±cols, clamped into range; cols clamped >=1).
                                                 Drives AdvancedSearchScreen's result panel.
               tile_thumb.*                    — shared tile-thumbnail draw (Phase 20 extract from
                                                 GalleryGrid): ThumbContext{vault,cache,worker,failed}
                                                 + draw_tile_thumb / tile_thumb_texture / tile_cover_tex.
                                                 Gallery -> folder + cover montage; image -> aspect-fit
                                                 thumb; video -> poster + play-badge. Decrypt ->
                                                 off-thread decode -> GPU upload via shared cache; no
                                                 new disk path. Reused by GalleryGrid (delegates) +
                                                 advanced-search grid view.
               tag_list_parse.*                — pure, SDL/vault-free tag-list parser (Phase 21):
                                                 parse_tag_list(span<const uint8_t>) -> normalised
                                                 tags (split on LF, trim CR+ws, drop blanks,
                                                 case-insensitive de-dupe keeping first casing,
                                                 truncate to TAG_MAX_BYTES=0xFFFF, cap at
                                                 INDEX_MAX_TAGS; non-UTF-8 bytes opaque). Unit-tested.
                                                 GalleryGrid Shift+G on a focused gallery tile opens a
                                                 .txt dialog (FileDialog Purpose::TagList +
                                                 open_tag_list) -> parse -> add_tag each (merge, not
                                                 replace); entry + result pump INLINED (free
                                                 apply_tag_list helper, counts added/skipped by tag-
                                                 count delta) to keep GalleryGrid under the S1448 cap.
               input.*, nav_model.*, viewer_model.h
               passphrase.*, screen.h
               secure_text_field.*, unlock_logic.*
  platform/    paths.*, file_dialog.*,         — config dirs, SDL file dialogs
                                                 (file_dialog has save_vault(), Phase 14).
                                                 Phase 17: each open tagged with a Purpose +
                                                 take_result(Purpose) so one shared dialog
                                                 polled by two handlers (GalleryGrid image vs
                                                 zip import) can't steal each other's result.
                                                 Phase 21: Purpose::TagList + open_tag_list() (.txt)
                                                 for the tag-list import.
               folder_dialog.*                 — export destination picker (Phase 10)
               vault_registry.*                — recent-vaults list (Phase 14): config-dir
                                                 file of known vault PATHS ONLY (no
                                                 secrets); list/add(move-to-front,dedup)/
                                                 remove/seed_if_empty; atomic temp+rename.
tests/
  crypto/ gfx/ image/ platform/ ui/ vault/ media/
  test_framework.h  test_main.cpp
vendor/
  SDL3/         git submodule, built by setup.sh (cmake)
  monocypher/   git submodule, single .c compiled by premake
  stb/          git submodule, header-only
  libwebp/ libde265/ libaom/ libheif/   image codecs (Phase 9), cmake-built static
  ffmpeg/       git submodule, configure-built static (Phase 15–16, audio decoders + H.264/H.265)
  miniz/        git submodule (pinned to master commit e78dfd2), plain-C ZIP reader compiled by
                premake (Phase 17). vendor/miniz-shim/miniz_export.h supplies the CMake-generated
                header so the submodule stays pristine; built with MINIZ_NO_ZLIB_COMPATIBLE_NAMES
  codecs-prefix/   staging install prefix for the codecs + FFmpeg (gitignored)
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
