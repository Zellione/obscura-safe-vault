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
                                               = 5 min). Phase 33 adds app/auto_lock.h:
                                               should_auto_lock(has_active, blocks_idle_lock,
                                               keep_unlocked, timer, dt) — pure, unit-tested
                                               extraction of maybe_auto_lock's 3 suppression
                                               guards (no active vault / a screen's
                                               blocks_idle_lock / the new session-only
                                               keep_unlocked_ toggle), all of which reset the
                                               IdleTimer instead of ticking it. keep_unlocked_
                                               is a plain App bool, flipped by GalleryGrid's
                                               `U` key via a new NavKind::ToggleKeepUnlocked
                                               (App::apply_nav flips it in place — no screen
                                               swap); always reset to false in
                                               promote_pending() and the LockActive nav case,
                                               so re-unlocking (even the same vault) always
                                               starts with auto-lock on. App::render_frame
                                               draws a small corner badge ("Auto-lock off
                                               [U]", free fn draw_keep_unlocked_badge) over
                                               whatever screen is active whenever
                                               active_ && keep_unlocked_ — an App-level
                                               overlay, not per-screen, so it stays visible
                                               across navigation without threading the flag
                                               through every screen's constructor.
                                               Phase 39 Part 2: App owns
                                               `ui::GallerySessionState session_` (mirrors
                                               adv_session_): last-used GalleryGrid List/Grid
                                               view + ImageViewer strip side + a single "last
                                               video watched" resume bookmark, carried across
                                               App's screen reconstruction on every nav
                                               transition. capture_session_state()
                                               (dynamic_cast onto whichever concrete Screen is
                                               active) snapshots it right before on_exit();
                                               to_gallery/to_viewer/to_favorite_viewer/
                                               to_tag_viewer feed session_.view/strip_side back
                                               in as the new screen's initial constructor arg.
                                               enter_viewer() is the shared tail of every
                                               ImageViewer-construction site: on_enter() then
                                               ui::apply_video_resume() to seek a reopened
                                               matching video to its bookmark (paused). Reset
                                               (session_.reset()) at LockActive, idle
                                               auto-lock, and promote_pending, exactly like
                                               adv_session_.
  crypto/      aead.*, kdf.*, random.*,        — Monocypher wrappers
               secure_mem.h, crypto.h
  vault/       vault.*, header.*, index.*,     — .osv container format
               chunk_store.*, byte_io.h, file_util.h
               safe_name.*                     — Phase 36. Pure node-name rules. A node name is a
                                                 single path COMPONENT, never a path.
                                                 is_safe_node_name = REJECT (vault ingress:
                                                 add_image/add_video/create_gallery — the API is the
                                                 trust boundary); sanitize_node_name = REPAIR
                                                 (importers: zip_plan basename_of/dir components,
                                                 meta_json titles, file_op_job picked files — an
                                                 awkward archive name must not fail a whole import).
                                                 sanitize's output ALWAYS satisfies is_safe_node_name
                                                 (property-tested). Rejects '/' AND '\' on every
                                                 platform, "."/"..", NUL/control/DEL, Windows-reserved
                                                 chars + device names (CON/NUL/COM1-9/LPT1-9),
                                                 trailing dot/space, >255 bytes (truncated on a UTF-8
                                                 codepoint boundary). Bytes >=0x80 stay opaque (CJK).
                                                 WHY: a .osv is UNTRUSTED INPUT (portable/shareable
                                                 via vault manager + transfer); index.cpp reads `name`
                                                 as opaque bytes, and ui::export_* turns it back into
                                                 a real path. `dest_dir / name` does NOT contain — an
                                                 ABSOLUTE name discards dest_dir outright. That was a
                                                 live arbitrary-file-write on export (CWE-22), found
                                                 while investigating two SonarCloud cpp:S2083 BLOCKERs
                                                 and proven by test (the unguarded build really did
                                                 write /tmp/pwned.png). Sink-side guard:
                                                 ui::export_path_within (weakly_canonical +
                                                 lexically_relative, fails closed) — needed because
                                                 vaults ALREADY on disk can carry hostile names, so
                                                 ingress validation alone is not enough.
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
                                                 Phase 25: transfer_images (bulk list driver,
                                                 TransferTally{done,failed}) + optional
                                                 vault::OpProgress* (op_progress.h; total/done +
                                                 cooperative cancel) on transfer_images/
                                                 transfer_gallery/export_images — a cancelled
                                                 gallery Move leaves the source intact.
                                                 ui::ImportProgress now aliases OpProgress.
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
                                                 Phase 22: tag_overview()->vector<ui::TagTally>
                                                 (per-distinct-tag direct {gallery,image} counts,
                                                 no cascade, reuses collect_tags vocab) +
                                                 galleries_with_tag()->galleries directly carrying
                                                 one tag + images_with_tag()->leaf media (images
                                                 AND videos) directly carrying one tag, no cascade
                                                 (all on VaultSearch for the S1448 cap);
                                                 read-time cascade (effective tags =
                                                 own ∪ ancestor galleries; root tags
                                                 global); resolve_node resolves a path
                                                 to a gallery OR image.
                                               — Phase 37: index.h adds a SortKey u8 on every
                                                 IndexNode (meaningful only on Gallery nodes:
                                                 Manual/NameAsc/NameDesc/DateAsc/DateDesc/
                                                 SizeAsc/SizeDesc), INDEX_VERSION=6 (v1–v5 read
                                                 back as Manual on every gallery — no visible
                                                 change until opt-in; out-of-range byte rejected,
                                                 not clamped). New ui/gallery_sort.{h,cpp} (pure,
                                                 mirrors natural_sort.h): sort_children(nodes,
                                                 SortKey) groups folders first, then Manual is a
                                                 no-op / NameAsc,Desc delegate to natural_less /
                                                 Date* compares created_ts / Size* compares
                                                 orig_size (all stable); next_sort_key cycles the
                                                 fixed Shift+S order; sort_key_label gives the
                                                 footer string (empty for Manual). vault::
                                                 gallery_sort_key(v,path) (getter) and
                                                 vault::set_gallery_sort(v,path,SortKey) (free
                                                 friends, persisted via commit_index); Vault::list
                                                 applies the target
                                                 gallery's own sort_key before returning, so every
                                                 caller (grid, list view, viewer thumbnail strip,
                                                 slideshow) gets one consistent order for free —
                                                 no call site re-implements sorting. gallery_sort_key/
                                                 set_gallery_sort are FREE FRIENDS (not members,
                                                 like read_thumb_span/vault_file_bytes) to stay
                                                 under the cpp:S1448 35-method cap.
               index_io.*                      — Internal component (Phase 25): index serialisation +
                                                 crash-safe double-buffer slot swap (append → write
                                                 inactive slot → flip active_slot, 3-phase atomic
                                                 commit). IndexIoContext bundles mutable state;
                                                 index_io::commit_index owns the persistence logic
                                                 extracted from Vault::commit_index — keeps Vault's
                                                 method count under the cpp:S1448 cap.
               vault_ops.*                     — Internal component (Phase 25): tree navigation + path
                                                 resolution + structural validation, extracted from
                                                 Vault (split_path, resolve_gallery, resolve_node_impl,
                                                 child_named, holds_media, holds_galleries,
                                                 for_each_media, relocate_node_chunks). Pure traversal,
                                                 no I/O. push_child(children, node) wraps the
                                                 vector::push_back add_image/add_video use to append an
                                                 IndexNode in try/catch (an allocation failure there is
                                                 the same uncaught-exception → terminate() bug class as
                                                 chunk_codec::resize_buf); returns false (→ IoError)
                                                 instead of crashing. push_child_fail_after mirrors
                                                 resize_fail_after's fault-injection pattern for tests.
               chunk_codec.*                   — Pure adaptive store-if-smaller deflate framing
                                                 (Phase 26): method byte (0=raw,1=deflate) + bounded
                                                 orig_len inside the AEAD; used by ChunkStore's framed
                                                 ctor flag (← header FLAG_FRAMED_CHUNKS bit) + the
                                                 index-blob sites (commit_index/unlock/compact). miniz
                                                 tdefl/tinfl, no new dep. The std::vector<uint8_t>
                                                 resize_buf overload wraps resize() in try/catch — it's
                                                 noexcept, and an uncaught allocation-failure exception
                                                 there would terminate() the whole process instead of
                                                 returning false like every other fallible call in this
                                                 codebase. resize_fail_after fault injection makes the
                                                 failure path deterministically testable. Legacy vaults
                                                 (header flag unset) read AND append raw forever, no
                                                 migration.
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
               audio_frame.h, volume_setting.*,  ChunkAvio/MemAvio = AVIOContext (read+seek, never
               loop_setting.*,                   a temp file); VideoDecoder = FFmpeg shared demuxer
               video_probe.*, decoded_frame.h
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
                                                 container/codec/dims/duration + first-frame poster;
                                                 best-effort — succeeds with placeholder Unknown/0/empty
                                                 metadata if the container is detected but the codec
                                                 isn't decodable yet. Vault::repair_video_metadata
                                                 (vault.h/.cpp, Phase 40 Part 1 bugfix) re-probes a node
                                                 stuck at that placeholder state and fills it in if the
                                                 codec has since become decodable; no-op otherwise. Test-
                                                 only friend vault::test_only_force_video_codec_unknown
                                                 (defined in tests/vault/test_video.cpp) constructs that
                                                 state for testing since add_video() can't produce it for
                                                 a currently-decodable file.
  gfx/         window.*, renderer.*,           — SDL3 window/renderer, texture cache,
               texture_cache.*, text.*,        — stb_truetype text atlas
               theme.{h,cpp}                   — UI colour tokens, runtime-selectable
                                                 (Phase 23): a `Theme` value + 4 presets
                                                 (Refined Slate default / Light / High
                                                 Contrast / Midnight); gfx::set_theme(id)/
                                                 active_theme() swap the active one, and the
                                                 `theme::X` tokens are references into it so
                                                 every call site tracks a switch. theme_slug/
                                                 theme_from_slug/theme_name are pure helpers.
                                                 RADIUS consts stay compile-time; renderer has
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
                                                 width reflow, centred/elided labels;
                                                 Shift+S cycles a gallery's persisted
                                                 sort_key (Phase 37), breadcrumb shows
                                                 "Sort: <label>" once non-Manual. Phase 39
                                                 Part 2: constructor gains an initial_view
                                                 param (default Grid); free friend
                                                 current_gallery_view(const GalleryGrid&)
                                                 (S1448 method-count reasons, mirrors
                                                 vault_busy) lets App read view_ on exit.
               image_viewer.*, widgets.*       — viewer has Fit + FillScroll + Slideshow
                                                 modes, bottom/left strip toggle (keys
                                                 F/T, P starts slideshow); widgets has
                                                 button_state + elide_middle + fit_text
                                                 (elide_middle bound to a FontAtlas). ImageViewer hosts
                                                 a fit-only VideoPlayback when the current item
                                                 is_video() (Phase 15: Space play/pause, J/L
                                                 +-5s, ,/. frame-step, drag seek bar).
                                                 Phase 16: M mute, volume ∓5%; seek bar
                                                 seeks both video + audio tracks in-sync. Phase 25:
                                                 volume via ui::volume_dir — `-`/`+` glyph keys (HUD
                                                 shows [-/+] Vol) + the `[`/`]` produced char resolved
                                                 through SDL_GetModState (so German AltGr+8/9 works) +
                                                 the physical bracket scancodes. The level is seeded
                                                 from media::saved_volume() on open + written back on
                                                 change, so it is remembered across clips + restarts
                                                 (platform::VolumePref config_dir()/volume.conf, one
                                                 float [0,1], atomic write, missing/invalid->1.0;
                                                 App loads at init + saves on clean exit; the in-memory
                                                 global lives in media/volume_setting.*, not AV-gated).
                                                 Phase 40 Part 1: R toggles loop (process-lifetime
                                                 media::saved_loop_enabled()/set_saved_loop_enabled(),
                                                 media/loop_setting.*, not persisted like volume);
                                                 VideoPlayback::advance()'s EOF branch re-seeks to 0
                                                 and keeps playing instead of pausing when loop_ is
                                                 set (same do_seek(0.0) path as the Space-at-end
                                                 replay); on-screen ring indicator next to the
                                                 play/pause icon (accent when on, dim when off, same
                                                 treatment as the mute speaker icon). AV1 decode
                                                 (`.webm`/`.mov`) added via the already-vendored
                                                 libaom as the `libaom-av1` FFmpeg decoder — FFmpeg's
                                                 own native "av1" decoder is a hwaccel-dispatch shim
                                                 only (ENOSYS without a HW accelerator, no software
                                                 decode); QTRLE/Cinepak added as two more native `.mov`
                                                 decoders (VideoCodec gains AV1/QTRLE/Cinepak).
                                                 GalleryGrid routes video imports to add_video +
                                                 draws a play badge (draw_tile_thumb) in grid & list.
                                                 Phase 39 Part 2: constructor gains an
                                                 initial_strip_side param (default Bottom); three
                                                 free friends (S1448 reasons) — current_strip_side,
                                                 capture_video_resume (snapshot outgoing viewer's
                                                 video path+position into a GallerySessionState, or
                                                 clear the bookmark when the current item isn't a
                                                 live video), apply_video_resume (seek a freshly
                                                 (re)opened matching video to a remembered position,
                                                 called right after on_enter() builds video_) — let
                                                 App carry strip-side + a video resume bookmark
                                                 across the grid<->viewer round trip.
               playback_model.*                — pure video transport maths: clock/clamp/
                                                 seek-bar map/mm:ss/frame-due (Phase 15,
                                                 pure/tested)
               video_playback.*                — in-viewer video player: VideoDecoder + YUV
                                                 texture + SDL_AudioStream (master audio clock)
                                                 + seek bar (both tracks); mute/volume via
                                                 SDL_SetAudioStreamGain; A/V sync via av_sync::decide;
                                                 pause pauses both; pImpl gated on OSV_VENDORED_AV
                                                 (non-AV build -> poster) (Phase 15–16). Phase 39
                                                 Part 2: seek(seconds) — clamped seek, does NOT
                                                 touch play/pause — restores a session resume
                                                 bookmark right after construction (playback
                                                 already opens paused by default).
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
                                                 GalleryGrid + ImageViewer (Phase 12). Current-tags
                                                 list scrolls (Up/Down) via pure tag_scroll.h
                                                 (tag_scroll_first) + auto-scrolls to a just-added
                                                 tag; previously clipped tags past the ~5 that fit
                                                 the fixed modal (Phase 21 fix). Phase 27 follow-up:
                                                 read-only "Inherited from gallery" section
                                                 (ui::inherited_tags, tag_inherit.*) below the
                                                 own-tags list; Del/selection never touch it.
                                                 Phase 29: autosuggest dropdown while typing
                                                 (VaultSearch::all_tags vocabulary refreshed on
                                                 open/add/remove; ui::editor_tag_suggestions;
                                                 Up/Down highlight via move_tag_cursor, -1 =
                                                 buffer; Enter adds the TYPED text unless a
                                                 suggestion is highlighted; Esc deselects first).
               tag_suggest.*                   — pure, SDL/vault-free autosuggest source (Phase
                                                 29): editor_tag_suggestions(buffer, vocab,
                                                 own_tags) — trim, rank via Phase 18
                                                 tag_suggestions, hide own tags (tag_ci_equal),
                                                 cap TAG_SUGGEST_MAX=5. Unit-tested.
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
               search_result_view.*            — Phase 20 decomposition extract: result grid+list view
                                                 state (ResultView{List,Grid} + toggle_result_view,
                                                 move_delta/move nav). Owns the off-thread decode
                                                 worker + feeds the thumbnail cache. Pure SDL-free state
                                                 machine, driven by advanced_search_screen's
                                                 on_update/handle_key.
               saved_search_panel.*            — Phase 20 decomposition extract: saved-search sidebar —
                                                 list rendering + CRUD (Ctrl+S save / Enter load / Del
                                                 delete). Pure vault/SDL-free; fed by
                                                 advanced_search_screen's query builder for
                                                 serialisation/suggestions. Together with
                                                 search_result_view.* this took AdvancedSearchScreen from
                                                 34 → 19 methods (cpp:S1448).
               favorites_images.*              — flat grid of favorited images across
                                                 the vault; opens a favorites-scoped
                                                 viewer (ToFavoriteViewer: prev/next
                                                 iterate the favorites set, Esc returns
                                                 to the grid) (Phase 13; F from gallery
                                                 grid). favorites_screen.* is the shared
                                                 base (grid/selection/badge) for both
                                                 favorites screens. Phase 22 f/u: it gained
                                                 handle_extra_key/extra_hint/show_favorite_badge
                                                 virtuals (default no-op/empty/true) used by the
                                                 tag_galleries + tag_images views.
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
                                                 `C` opens the ThemePicker overlay (Phase 23).
               theme_picker.*                  — `C` overlay over the vault manager (Phase 23,
                                                 QuickSwitch-style): Up/Down previews a built-in
                                                 theme live (gfx::set_theme) + persists it
                                                 (platform::ThemePref) — the preview IS the choice;
                                                 Enter/Esc close. Each row shows an accent swatch.
               transfer_dialog.*               — `M` modal in GalleryGrid (Phase 14 PR2/3/4):
                                                 move OR copy selected images / (PR3) a focused
                                                 gallery subtree to another vault — or (PR4)
                                                 within the active vault. Source enum
                                                 {Images,Gallery}; open()/open_gallery().
                                                 Stages: Mode(Move/Copy) → pick dest vault
                                                 ("This vault" row + registry minus active;
                                                 "This vault" skips unlock, dest_is_self_,
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
               natural_sort.*                  — pure, SDL/vault-free natural-order name comparator
                                                 (Phase 24, unit-tested): natural_compare (3-way:
                                                 digit runs by value so "2"<"10", other chars
                                                 case-insensitive, fewer leading zeros first) +
                                                 natural_less. Orders CBZ pages by reading order.
               gallery_sort.*                  — pure, SDL/vault-free per-gallery sort presentation
                                                 (Phase 37, mirrors natural_sort.h/tag_overview_
                                                 model.h): sort_children(children, SortKey) —
                                                 folders always precede images/videos, then within
                                                 each group Manual is a no-op, NameAsc/NameDesc
                                                 delegate to natural_less (image1/image2/.../
                                                 image10 sorts numerically), Date* compares
                                                 created_ts, Size* compares orig_size; stable
                                                 throughout. next_sort_key cycles the fixed
                                                 Shift+S order (Manual→NameAsc→NameDesc→DateAsc→
                                                 DateDesc→SizeAsc→SizeDesc→Manual); sort_key_label
                                                 gives the footer string (empty for Manual, so the
                                                 indicator hides at the default). Used by both
                                                 Vault::list (every listing call site) and
                                                 GalleryGrid's footer/HUD.
               tag_inherit.*                   — pure, SDL-free ancestor-gallery tag union (Phase 27
                                                 follow-up): inherited_tags(vault, node_path) —
                                                 root→parent order, ci de-dupe, minus own tags.
                                                 Feeds the tag editor's read-only "Inherited from
                                                 gallery" section. Unit-tested.
               video_repair.*                  — Phase 40 Part 1 bugfix: repair_unknown_video_metadata
                                                 (vault, gallery_path, children) sweeps a freshly-
                                                 vault.list()'d gallery for video children still at
                                                 VideoCodec::Unknown (imported before their codec was
                                                 decodable) and calls Vault::repair_video_metadata per
                                                 node. Called from GalleryGrid::refresh() so previously-
                                                 imported videos self-heal (thumbnail + duration) the
                                                 next time their gallery is opened — no separate
                                                 migration step. Unit-tested (SDL-free, real Vault).
               meta_json.*                     — pure, SDL/vault-free archive `meta.json` parser
                                                 (Phase 27, nlohmann/json vendored header-only):
                                                 parse_meta_json (tolerant, exception-free:
                                                 malformed/wrong types/unknown keys -> empty
                                                 fields) -> ArchiveMeta{title_english,
                                                 title_japanese, tags["type:name"; bare name for
                                                 the generic type "tag"/"tags"]};
                                                 meta_gallery_name (english -> japanese ->
                                                 fallback; '/'->'_') + meta_gallery_tags
                                                 (japanese title first, kept searchable).
                                                 Unit-tested.
               zip_plan.*                      — pure ZIP placement planner (Phase 17):
                                                 archive entries -> galleries to create +
                                                 file placements + mixed-folder conflicts +
                                                 skip count. SDL-/miniz-/vault-free, unit-tested.
                                                 ZipDest{NewGallery,Append}, ZipConflictPolicy
                                                 {AskUser,FlattenMixed,SkipMixed}. Phase 24:
                                                 is_supported_image_name (image subset, not video)
                                                 + build_cbz_plan -> a fixed one-leaf plan (gallery
                                                 named after the archive) of every image entry,
                                                 videos/other skipped+counted, subfolders flattened
                                                 (basename collisions disambiguated by source dir),
                                                 natural reading order. Phase 27: find_meta_entry —
                                                 a top-level meta.json (case-insensitive, files
                                                 only) is excluded by every planner path (never
                                                 placed, never counted skipped).
               zip_encoding.*                  — decode_zip_entry_name (Phase 36 part 2): legacy
                                                 (non-UTF-8) zip/cbz entry-name decoding. When a
                                                 name lacks the general-purpose UTF-8 bit (bit 11),
                                                 decodes via a fixed 128-entry CP437->Unicode table
                                                 (bytes 0x80-0xFF; 0x00-0x7F are ASCII-identical),
                                                 unless the raw bytes already parse as valid UTF-8
                                                 (passed through unchanged rather than re-decoded
                                                 into mojibake). Shift_JIS/other double-byte legacy
                                                 encodings are out of scope — such a name still
                                                 imports safely, just mis-decoded as CP437. Pure,
                                                 SDL/vault-free, unit-tested; used by
                                                 zip_import.cpp's read_entry_list.
               zip_import.*                    — ZIP/CBZ import executor (Phase 17; CBZ Phase 24):
                                                 miniz reader -> mlock'd SecureBytes (one entry at
                                                 a time, no temp file) -> Vault::add_image/add_video
                                                 chosen by image::detect_format. needs_resolution for
                                                 mixed folders. import_cbz (Phase 24) reuses the same
                                                 per-entry path over build_cbz_plan — `.cbz` imports
                                                 as one page gallery, never extracted to disk. Lives
                                                 in ui/ like export.* (deps vault + image). Hosted by
                                                 GalleryGrid (Z key; dialog filter accepts zip;cbz,
                                                 .cbz routes to a fixed one-leaf import).
                                                 import_zip/import_cbz take an optional
                                                 ImportProgress* (atomic total/done/cancel) so a
                                                 caller can drive a progress bar + cooperative cancel.
                                                 Phase 27: a top-level meta.json seeds the created
                                                 gallery's tags (japanese title + each "type:name")
                                                 via Vault::add_tag (zip NewGallery top gallery /
                                                 cbz leaf); Append just excludes the file. The title
                                                 is NOT applied by the importer: peek_archive_meta
                                                 reads the meta at file-pick time and GalleryGrid
                                                 prefills the name popup with meta_gallery_name(meta,
                                                 stem) — the confirmed popup text is authoritative.
                                                 Extracted into mlock'd memory, 1 MiB cap; malformed
                                                 meta.json never blocks the import.
               archive_reader.*                — ArchiveReader (Phase 34): thin wrapper over
                                                 libarchive's streaming read API
                                                 (archive_read_open_memory over an mlock'd buffer ->
                                                 archive_read_next_header/archive_read_data), whole-
                                                 file gated OSV_VENDORED_ARCHIVE (mirrors
                                                 media/video_decoder.h). open() does one forward pass
                                                 building entries() (reuses ZipEntry from zip_plan.h
                                                 verbatim — already format-agnostic); extract(index,
                                                 out) re-opens a fresh stream + walks forward to index
                                                 EACH call (libarchive has no random-access read API)
                                                 — O(n) per extract, fine for gallery-sized archives.
                                                 MAX_ENTRY_BYTES=4 GiB bomb guard checked against the
                                                 entry's declared size before allocating (mirrors
                                                 chunk_codec's orig_len check).
               archive_import.*                — import_archive/import_archive_cbz (Phase 34): mirrors
                                                 zip_import.*'s import_zip/import_cbz structure but
                                                 backed by ArchiveReader, covering .7z/.rar/
                                                 .tar(+.gz/.xz)/.cbr/.cb7/.cbt. Declared unconditionally
                                                 (no #ifdef at call sites); the .cpp internally branches
                                                 on OSV_VENDORED_ARCHIVE, returning a graceful "not
                                                 supported" ZipImportOutcome without it (mirrors
                                                 ui::VideoPlayback's non-AV poster-only fallback) so
                                                 gallery_grid.cpp needs zero #ifdefs. GalleryGrid's
                                                 classify_archive_ext() picks the miniz vs libarchive
                                                 backend + the CBZ-style vs mirror/append plan purely
                                                 from the file extension.
               zip_import_job.*                — ZipImportJob: runs import_cbz/import_zip
                                                 (start_cbz/start_zip, shared launch() helper) on a
                                                 background std::thread so a big archive (~10 s of
                                                 decode+encrypt) never freezes the UI on the name
                                                 popup ("locked in" bug, Phase 24 fix). Phase 34 adds
                                                 start_archive/start_archive_cbz — thin wrappers over
                                                 the same launch() calling import_archive/
                                                 import_archive_cbz instead; no separate job class.
                                                 Contract:
                                                 while active() the worker owns the vault's single-
                                                 thread file handle, so GalleryGrid must NOT touch
                                                 the vault (update()/render()/handle_event() short-
                                                 circuit — no thumbnail reads/listing); it polls
                                                 total()/done() + take_outcome() (joins on completion)
                                                 and draws a progress modal only, Esc ->
                                                 cancel(). A ZIP with mixed folders comes back
                                                 needs_resolution (nothing written); poll_import_job
                                                 keeps naming_.zip active for the Flatten/Skip modal +
                                                 F/S re-launch with the chosen policy. Screen gained a
                                                 blocks_idle_lock() hook (default false; GalleryGrid
                                                 returns import_job_.active()) so App::maybe_auto_lock
                                                 can't wipe the master key mid-write. Compiled into
                                                 osv_tests (unit-tested via a poll-to-completion
                                                 harness, cbz + zip incl. needs_resolution).
               keybindings.h                   — pure layout-independent key resolution (Phase 25,
                                                 unit-tested): bracket_key_for_scancode maps the two
                                                 physical keys right of `P` -> BracketKey{Decrease,
                                                 Increase} by SDL SCANCODE, so video volume `[`/`]`
                                                 + slideshow dwell work on any layout (German QWERTZ
                                                 has those glyphs behind AltGr). Also centralises the
                                                 character-resolved is_search_key/is_advanced_search_key/
                                                 is_quick_switch_key helpers (moved from quick_switch.h).
                                                 video_playback + slideshow_view handle_key now take
                                                 the scancode; gallery_grid/image_viewer use the helpers.
               file_op_job.*                   — FileOpJob (Phase 25): runs export / delete / move-copy
                                                 on a background worker (mirrors ZipImportJob). Same
                                                 single-thread vault-handle contract — while active()
                                                 the worker owns the vault(s); host screen only polls
                                                 total()/done() + take_outcome() (joins) + draws a modal,
                                                 Esc->cancel(). start_export/start_delete/
                                                 start_transfer_images/start_transfer_gallery ->
                                                 FileOpOutcome{ok,cancelled,done,failed,status,...}.
                                                 GalleryGrid owns one (naming_.file_op) for export+delete;
                                                 TransferDialog owns one (Running stage). ImageViewer's
                                                 single-image export stays synchronous (fast). The
                                                 GalleryGrid gate (vault_busy/poll_file_job/handle_job_input)
                                                 is free friends; blocks_idle_lock() holds off idle lock.
                                                 Compiled into osv_tests (poll-to-completion harness).
               waste_threshold.h               — pure vault-bloat thresholds (#48 audit):
                                                 should_display_waste(wasted, file_size) — true if waste
                                                 exceeds max(50 MiB, 10% of file_size); should_hint_
                                                 cancelled_import_waste(wasted) — true if > 1 MiB. Drives
                                                 GalleryGrid's Shift+C compact-confirm footer hint.
               progress_modal.*                — draw_op_progress: shared veil + "N/M" bar + cancel-hint
                                                 modal reused by every screen hosting a background job
                                                 (import/export/delete/move) (Phase 25).
               help_popup.*                    — the shared `F1` help popup (Phase 39): HelpGroup/HelpEntry
                                                 data types + pure open/close/scroll logic + draw_help_popup
                                                 rendering. Screen::help_groups() virtual (default empty)
                                                 supplies per-screen grouped content; App owns HelpPopupState,
                                                 intercepts F1 globally, renders overlay on top (mirroring
                                                 Phase 33 keep-unlocked corner-badge). Esc/Q close.
               gallery_view.h                  — GalleryView{Grid,List} (Phase 39 Part 2 extract
                                                 from GalleryGrid's private nested enum, now
                                                 shared so App/GallerySessionState can name it).
               gallery_session_state.h         — GallerySessionState{view,strip_side,
                                                 last_media_path,video_resume_seconds} + reset()
                                                 (Phase 39 Part 2, modeled on AdvancedSearchState,
                                                 see mem note below); pure, unit-tested, App-owned.
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
                                                 thumb; video -> poster + play-badge. Phase 39:
                                                 thumb_key_for helper (pure index lookup) fixes video
                                                 posters never showing as thumbnails. Decrypt ->
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
               tag_overview_model.*            — pure, SDL/vault-free tag-overview presentation
                                                 (Phase 22, unit-tested): TagTally{tag,gallery_count,
                                                 image_count} + sort_tags(Name / Count-desc) +
                                                 filter_tags(case-insensitive prefix).
               tag_overview.*                  — `Shift+T` first-class Screen (NavKind::ToTagOverview):
                                                 scrollable tag list (Up/Down, Enter, Tab=toggle sort,
                                                 type=prefix filter, `=quick-switch); counts from
                                                 VaultSearch::tag_overview, sort/filter from
                                                 tag_overview_model; Enter -> TagGalleries (Phase 22).
               tag_galleries.*                 — galleries-only view of the galleries directly
                                                 carrying one tag (NavKind::ToTagGalleries, tag in
                                                 Nav::path); thin FavoritesScreen subclass over
                                                 VaultSearch::galleries_with_tag whose go_back()
                                                 (new virtual on FavoritesScreen) returns to the
                                                 overview, not the root grid (Phase 22). Tab toggles
                                                 to the images face (NavKind::ToTagImages) via the
                                                 FavoritesScreen handle_extra_key hook (Phase 22 f/u).
               tag_images.*                    — images/videos directly carrying one tag (Phase 22
                                                 follow-up; NavKind::ToTagImages, tag in Nav::path).
                                                 TagImages : public FavoritesImages — reuses its
                                                 off-thread thumb decode + tile draw; fetch() ->
                                                 VaultSearch::images_with_tag. Tab toggles back to
                                                 the galleries face; Enter opens a collection viewer
                                                 over the tagged set (NavKind::ToTagViewer; App::
                                                 to_tag_viewer mirrors to_favorite_viewer, back ->
                                                 ToTagImages); go_back() returns to the tag overview.
                                                 No favorite badge (show_favorite_badge=false).
               input.*, nav_model.*, viewer_model.h
               passphrase.*, screen.h               — Phase 39: Screen::help_groups() virtual (default
                                                 empty) — per-screen grouped shortcuts for the F1 help
                                                 popup. GalleryGrid, ImageViewer, FavoritesScreen,
                                                 TagOverviewScreen, AdvancedSearchScreen, VaultManager,
                                                 UnlockScreen all override it.
               secure_text_field.*, unlock_logic.*
  platform/    paths.*, file_dialog.*,         — config dirs, SDL file dialogs
                                                 (file_dialog has save_vault(), Phase 14).
                                                 Phase 17: each open tagged with a Purpose +
                                                 take_result(Purpose) so one shared dialog
                                                 polled by two handlers (GalleryGrid image vs
                                                 zip import) can't steal each other's result.
                                                 Phase 21: Purpose::TagList + open_tag_list() (.txt)
                                                 for the tag-list import. Phase 24: open_zip()'s
                                                 filter accepts zip;cbz.
               folder_dialog.*                 — export destination picker (Phase 10)
               vault_registry.*                — recent-vaults list (Phase 14): config-dir
                                                 file of known vault PATHS ONLY (no
                                                 secrets); list/add(move-to-front,dedup)/
                                                 remove/seed_if_empty; atomic temp+rename.
               theme_pref.*                    — chosen UI theme persistence (Phase 23):
                                                 config_dir()/theme.conf holds the theme's
                                                 stable slug ONLY (no secrets); load()->ThemeId
                                                 (missing/unknown -> default), save(id); atomic
                                                 temp+rename, mirrors vault_registry. Loaded in
                                                 App::init(), saved live by ThemePicker.
               harden.{h,cpp}                  — disable_core_dumps(): prctl(PR_SET_DUMPABLE,0) +
                                                 setrlimit(RLIMIT_CORE,{0,0}) on Linux, no-op on
                                                 Windows (macOS support removed — see the `#error`
                                                 guard in src/crypto/random.cpp); called once at app
                                                 init, Release (NDEBUG) builds only, before any vault
                                                 unlock, to keep decrypted data/keys out of core dumps.
                                                 Also: redirect_stream_to_file/
                                                 redirect_diagnostics_to_log_file (Windows Release only —
                                                 a windowless WindowedApp process has no valid stdout/
                                                 stderr handle, so every std::println(stderr,...) would
                                                 throw std::system_error and terminate(); redirects both
                                                 to config_dir()/console.log instead).
               error_log.*                     — persistent, best-effort error log (video-import crash
                                                 fix): log_error(tag,msg) appends "[tag] msg" to both
                                                 stderr (Debug's console) and config_dir()/error.log
                                                 (Release has no visible console otherwise).
                                                 install_terminate_logger() installs std::set_terminate
                                                 so an uncaught exception logs what() before the process
                                                 dies instead of vanishing with zero trace; called first
                                                 in App::init(). Never logs decrypted plaintext or key
                                                 material (invariant #5).
               safe_print.h                    — platform::safe_println<Args...>(stream, fmt, args...):
                                                 wraps std::println in try/catch, swallowing any
                                                 std::system_error from a failed write. std::println
                                                 throwing on a closed/invalid stream (Windows Release's
                                                 windowless stdout/stderr) previously crashed the process
                                                 via terminate() from INSIDE error_log.cpp's own
                                                 terminate handler; every diagnostic print call site
                                                 must go through this wrapper instead of raw std::println.
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
  zlib/ xz/ libarchive/   7z/RAR/TAR read support (Phase 34), cmake-built static -> codecs-prefix.
                zlib (gzip filter) + xz/liblzma (LZMA2 filter) are libarchive's own filter deps,
                found via CMAKE_PREFIX_PATH like libheif finds libde265/libaom. libarchive's
                out-of-tree build dir is vendor/.libarchive-build (gitignored), NOT
                vendor/libarchive/build — that path is already tracked by the submodule itself
                (cmake helper modules) and an out-of-tree build there would clobber it. bzip2
                dropped from scope: upstream ships no CMake build.
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
6. A vault file is untrusted input (portable/shareable); node names are path
   components, never paths — validate on ingress (`vault::is_safe_node_name`),
   repair on import (`vault::sanitize_node_name`), containment-check on export
   (`ui::export_path_within`); never build `dest_dir / node.name` directly
   (CWE-22, Phase 36).

## Key hierarchy

`KEK = Argon2id(password [‖ keyfile], salt)` → unwraps random 32-byte master key.
All data/thumbnail/index chunks encrypted with master key + fresh nonce per chunk.

## Vault write atomicity

Append chunks → fsync → write index to inactive slot → fsync → flip `active_slot` → fsync.

## Tech stack details: `mem:tech_stack`
## Build/run/test commands: `mem:suggested_commands`
## Code conventions: `mem:conventions`
## Task-completion checklist: `mem:task_completion`
## Full `.osv` binary layout (header/chunk/index byte fields): `mem:vault_format`
## UI/UX specification (screen designs, help popup convention): `mem:ui_spec`
