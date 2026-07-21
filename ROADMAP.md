# Obscura-Safe-Vault — ROADMAP

> **Legend**
> - ✅ Done   🔜 In progress / planned   ⬜ Not started
> - Each phase ends with a clear acceptance criterion that must pass before work on the next phase begins.
> - Full spec for each phase (goal, tasks, acceptance criterion, status, follow-ups) lives in its own file under `docs/roadmap/`. This index is a scannable summary — open the linked file when working on that phase.

---

## Phase index

| # | Name | Status | Summary |
|---|---|---|---|
| 0 | Skeleton & minimal window | ✅ | Project structure, build system, a compilable app that opens a window. → [details](docs/roadmap/phase-00-skeleton.md) |
| 1 | Crypto core | ✅ | Full cryptographic primitive layer, implemented and tested. → [details](docs/roadmap/phase-01-crypto-core.md) |
| 2 | Vault container | ✅ | The `.osv` file format: header, index tree, chunk store, core vault API. → [details](docs/roadmap/phase-02-vault-container.md) |
| 3 | Image decode & thumbnails | ✅ | Decode images from decrypted memory buffers, generate encrypted thumbnails. → [details](docs/roadmap/phase-03-image-decode-thumbnails.md) |
| 4 | Graphics layer | ✅ | GPU texture cache and text atlas for the UI. → [details](docs/roadmap/phase-04-graphics-layer.md) |
| 5 | Unlock screen & gallery grid | ✅ | Vault layer connected to the UI — create/open/unlock a vault, browse galleries. → [details](docs/roadmap/phase-05-unlock-screen-gallery-grid.md) |
| 6 | Image viewer | ✅ | Full-screen image viewing with zoom/pan and the auto-scrolling thumbnail strip. → [details](docs/roadmap/phase-06-image-viewer.md) |
| 7 | Hardening & polish | ✅ | Close security gaps, handle edge cases, add deletion + compaction. → [details](docs/roadmap/phase-07-hardening-polish.md) |
| 8 | Cross-platform ports | ✅ | Windows and (later-removed) macOS build configs + CI pipeline. → [details](docs/roadmap/phase-08-cross-platform-ports.md) |
| 9 | Extra image formats | ✅ | WebP and HEIC/AVIF support. → [details](docs/roadmap/phase-09-extra-image-formats.md) |
| 10 | Export (selective, hard-gated) | ✅ | Deliberately extract decrypted images out of the vault, consent-gated. → [details](docs/roadmap/phase-10-export.md) |
| 11 | Slideshow | ✅ | Auto-advancing full-screen viewing of a leaf gallery. → [details](docs/roadmap/phase-11-slideshow.md) |
| 12 | Tags & Search | ✅ | Per-node tags on images and galleries, with cascade + live search. → [details](docs/roadmap/phase-12-tags-search.md) |
| 13 | Favorites | ✅ | Mark images/galleries as favorite, browse via two dedicated screens. → [details](docs/roadmap/phase-13-favorites.md) |
| 14 | Multiple vaults | ✅ | Manage and open several vaults; move images between them. → [details](docs/roadmap/phase-14-multiple-vaults.md) |
| 15 | Video playback (frames + seek) | ✅ | Store videos in the vault, play the video track, seek. → [details](docs/roadmap/phase-15-video-playback.md) |
| 16 | Audio & A/V sync | ✅ | Audio track added to the video pipeline with proper sync. → [details](docs/roadmap/phase-16-audio-av-sync.md) |
| 17 | Import ZIP archives | ✅ | Import a `.zip` of photos/videos into the vault in one action. → [details](docs/roadmap/phase-17-import-zip-archives.md) |
| 18 | Advanced search (dedicated screen) | ✅ | Weighted include/exclude + AND/OR groups + saved searches. → [details](docs/roadmap/phase-18-advanced-search.md) |
| 19 | Gallery cover thumbnails | ✅ | Representative cover art instead of the generic folder icon. → [details](docs/roadmap/phase-19-gallery-cover-thumbnails.md) |
| 20 | Advanced-search list/grid result views | ✅ | Toggle the advanced-search result panel between list and grid. → [details](docs/roadmap/phase-20-advanced-search-result-views.md) |
| 21 | Import a tag list onto a gallery | ✅ | Bulk-add tags to a gallery from a plain-text file. → [details](docs/roadmap/phase-21-import-tag-list.md) |
| 22 | Tag overview screen | ✅ | Dedicated screen listing every distinct tag with counts. → [details](docs/roadmap/phase-22-tag-overview-screen.md) |
| 23 | UI colour schemes | ✅ | Several selectable, runtime-switchable UI themes. → [details](docs/roadmap/phase-23-ui-colour-schemes.md) |
| 24 | Import CBZ archives | ✅ | Import a `.cbz` comic archive as a single page gallery. → [details](docs/roadmap/phase-24-import-cbz-archives.md) |
| 25 | Bugfixes & housekeeping | ✅ | Layout-independent keybindings, unified background-job progress UX. → [details](docs/roadmap/phase-25-bugfixes-housekeeping.md) |
| 26 | Transparent vault compression | ✅ | Adaptive store-if-smaller deflate framing before encryption. → [details](docs/roadmap/phase-26-vault-compression.md) |
| 27 | `meta.json` metadata on archive import | ✅ | Archive `meta.json` seeds the imported gallery's title + tags. → [details](docs/roadmap/phase-27-meta-json-metadata.md) |
| 28 | Broaden `.mov` / video codec support | ✅ | Decode ProRes/DNxHD/MJPEG beyond H.264/H.265. → [details](docs/roadmap/phase-28-mov-codec-support.md) |
| 29 | Tag autosuggest in the tag editor | ✅ | Autocomplete dropdown while typing a tag. → [details](docs/roadmap/phase-29-tag-autosuggest.md) |
| 30 | Import PDF as a gallery of pages | 🔜 | Import a `.pdf` as a gallery of rendered page images, like CBZ. → [details](docs/roadmap/phase-30-import-pdf.md) |
| 31 | Fullscreen viewing + edge-click navigation | ✅ | Borderless fullscreen + click-to-advance in the viewer. → [details](docs/roadmap/phase-31-fullscreen-viewing.md) |
| 32 | Background multi-file import | ✅ | Multi-select file-picker import runs on a background job. → [details](docs/roadmap/phase-32-background-multi-import.md) |
| 33 | Keep a vault unlocked for the session | ✅ | Session-only opt-out of the idle auto-lock timer. → [details](docs/roadmap/phase-33-keep-unlocked-session.md) |
| 34 | Import 7z, RAR, and TAR archives | ✅ | Extends ZIP/CBZ import to `.7z`/`.rar`/`.tar` via libarchive. → [details](docs/roadmap/phase-34-import-7z-rar-tar.md) |
| 35 | Password-protected archive import (ZIP/CBZ) | ✅ | Import a password-protected `.zip`/`.cbz`. → [details](docs/roadmap/phase-35-password-protected-archives.md) |
| 36 | Robust special-character filename & archive-name handling | ✅ | Safe node-name rules + legacy CP437 zip entry-name decoding. → [details](docs/roadmap/phase-36-special-character-filenames.md) |
| 37 | Persisted per-gallery sort order | ✅ | Choose + persist a gallery's sort order, including natural name order. → [details](docs/roadmap/phase-37-persisted-sort-order.md) |
| 38 | WebM video support (VP8/VP9) | ✅ | Import and play `.webm` video (Matroska + VP8/VP9). → [details](docs/roadmap/phase-38-webm-video-support.md) |
| 39 | Discoverable shortcuts & session-scoped gallery memory | ✅ | `F1` help popup + session-scoped gallery/viewer state memory. → [details](docs/roadmap/phase-39-discoverable-shortcuts-session-memory.md) |
| 40 | Video codec/loop/sync polish, gallery position memory & view density | ✅ | Part 1 ✅: AV1 + broader `.mov` codecs, video loop toggle, A/V sync hardening, + bugfix ✅: self-healing metadata repair for videos imported before their codec was decodable. Part 2 ✅: session-scoped gallery position memory (descend/ascend/leave-and-return restores the last-selected tile at every level). Part 3 ✅: 5-way List/Grid S-XL view density. → [details](docs/roadmap/phase-40-video-gallery-browsing-polish.md) |
| 41 | Async video decode | ✅ | Move CPU-heavy video codec decode off the render thread onto a background worker, so slow codecs (AV1/HEVC) don't stall playback/input/A-V sync. → [details](docs/roadmap/phase-41-async-video-decode.md) |
| 42 | ThreadSanitizer CI leg | ✅ | New `--tsan` build option + `tests-tsan` CI job, running the full suite under ThreadSanitizer on every PR to directly validate Phase 41's concurrent code (and any future threading) — reuses the plain vendored codec/SDL3 build rather than a parallel sanitizer-instrumented prefix. → [details](docs/roadmap/phase-42-tsan-ci.md) |
| 43 | Platform hardware-accelerated video decode | ✅ | Part 1 ✅: shared `media::HwAccelContext` infra + Windows D3D11VA, software `VideoDecodeWorker` as the automatic fallback. Part 2 ✅: VAAPI dlopen shim (`vendor/vaapi-shim`) + Linux enablement (`vendor/libva`, headers-only). → [details](docs/roadmap/phase-43-hardware-video-decode.md) |
| 44 | Gallery organization tools | ✅ | Part 1: scrollable + filterable Move-dialog gallery picker. Part 2: rename images/videos/galleries. Part 3: mass-move extended to galleries. Part 4: combine (recursive merge) two galleries, same- or cross-vault. → [details](docs/roadmap/phase-44-gallery-organization.md) |
| 45 | Organization, security & fullscreen polish | ✅ | Rename extended to favorites/tag-overview/search-result screens, mass tag add/remove, clipboard copy for password/passphrase, fullscreen hides the thumbnail strip, bigger video seek-bar hit target, auto-lock-off badge fades after 10s. → [details](docs/roadmap/phase-45-organization-ux-polish.md) |
| 46 | Mixed galleries (images + videos + sub-galleries together) | ✅ | Relax the leaf-only invariant so a gallery can hold any combination of media and sub-galleries, like a real folder. → [details](docs/roadmap/phase-46-mixed-galleries.md) |
| 47 | Animated GIF support | ✅ | Animated GIFs animate in the viewer (Space pauses) and on the hovered grid/strip tile, and carry an "A" badge. FFmpeg gif decoder + a new `animated` index flag (`INDEX_VERSION` 7). → [details](docs/roadmap/phase-47-animated-gifs.md) |
$1| 49 | Colour-coded tag chips & per-vault settings | ⬜ | Tags render as a coloured dot + bare name instead of `category:name`, on every tag surface. New global `F2` settings overlay (sidebar + pane) configures the per-vault category→colour mapping and a vault-wide default sort order; theme moves in from the deleted `C` picker. New vault-global settings block + explicit `Insertion` sort key (`INDEX_VERSION` 8). → [details](docs/roadmap/phase-49-tag-chips-settings.md) |

---

## Container format spec (reference)

Reproduced from `mem:vault_format` (Serena) for quick access during vault
implementation work.

```
Offset  Size  Description
──────  ────  ───────────────────────────────────────────────────────────────
0       8     magic: "OSVAULT\0"
8       2     version (u16, currently 1)
10      2     header_size (u16, total header length in bytes)
12      4     flags (u32, reserved)

16      1     kdf_algo  (0 = Argon2id)
17      4     t_cost    (u32, Argon2id time cost)
21      4     m_cost_kib (u32, Argon2id memory cost in KiB)
25      4     parallelism (u32)
29      16    salt      (u8[16], random)
45      1     keyfile_required (u8, 0 or 1)

46      24    master_key_nonce (u8[24])
70      32    wrapped_master_key (u8[32], XChaCha20-Poly1305 ciphertext)
102     16    master_key_tag (u8[16], Poly1305 tag)

118     8     slot_a_offset (u64)
126     8     slot_a_length (u64)
134     24    slot_a_nonce  (u8[24])
166     8     slot_b_offset (u64)
174     8     slot_b_length (u64)
182     24    slot_b_nonce  (u8[24])
206     1     active_slot   (u8, 0 = A, 1 = B)

207     N     reserved padding (zeroes, up to header_size)
```

**Data region** starts at `header_size`. Each encrypted chunk is laid out as:
```
  nonce[24] | ciphertext[plaintext_len] | tag[16]
```

**Framed vaults (header flags bit 0, Phase 26):** the AEAD plaintext of every
chunk AND of the sealed index blob is a chunk_codec frame:
```
  method u8 (0 = raw, 1 = deflate)
  if raw:     payload bytes
  if deflate: orig_len u64 LE | zlib-wrapped deflate stream
```
Flag clear (legacy): the plaintext is the payload verbatim, read and appended that
way forever.

**Index blob** (binary serialised; `INDEX_VERSION = 6`):
```
  version    u8
  root       node              (the tree, recursive — see below)
  saved_searches (v5+):                (Phase 18; omitted in v1–v4 blobs → empty)
    count    u16
    entries  { name_len u16; name u8[name_len];
               query_len u32; query u8[query_len] } [count]
```

**Index tree node** (binary serialised):
```
  node_type  u8  (0 = gallery, 1 = image, 2 = video)
  name_len   u16
  name_len   u16
  name       u8[name_len]  (UTF-8)

  tag_count  u16                     (Phase 12; v2+. Omitted entirely in v1 blobs.)
  tags       { tag_len u16; tag u8[tag_len] (UTF-8) } [tag_count]

  favorite   u8                      (Phase 13; v3+. Omitted in v1/v2 blobs → 0.)

  if gallery:
    sort_key     u8                 (Phase 37; v6+. Manual/NameAsc/NameDesc/
                                      DateAsc/DateDesc/SizeAsc/SizeDesc.
                                      Omitted in v1–v5 blobs → Manual.)
    child_count  u32
    children     node[child_count]  (recursive)

  if image:
    format       u8  (0=JPEG, 1=PNG, 2=GIF, 3=BMP, 4=TGA, 5=HDR, 6=WebP, 7=HEIC, 8=AVIF)
    width        u32
    height       u32
    orig_size    u64  (plaintext bytes)
    created_ts   u64  (Unix timestamp, seconds)
    data_offset  u64
    data_length  u64
    thumb_offset u64
    thumb_length u64
```

> **Format extensions (Phases 12–18).** The index serialisation is
> versioned; each of these bumps `INDEX_VERSION` and reads older versions with
> the new fields defaulted, so existing vaults keep opening cleanly:
> - **Phase 12 (Tags):** ✅ shipped — a tag list (`u16 count` + length-prefixed
>   UTF-8) on **both** gallery and image nodes, written after `name`
>   (`INDEX_VERSION = 2`; v1 blobs read with empty tags). Gallery tags cascade to
>   descendants at read time (effective tags = own ∪ ancestors'); they are not
>   copied onto children.
> - **Phase 13 (Favorites):** ✅ shipped — a `favorite u8` flag on both node types,
>   written after the tag block (`INDEX_VERSION = 3`; v1/v2 blobs read as
>   not-favorited).
> - **Phases 15–16 (Video):** a video node kind (a `media_kind` discriminator)
>   and new `format` codes appended after `8=AVIF` (e.g. `9=MP4/H.264`); video
>   nodes reuse the same `data_*`/`thumb_*` layout (thumb = poster frame), with
>   the container stored across multiple encrypted chunks.
> - **Phase 18 (Advanced search):** ✅ shipped — a **vault-global saved-searches
>   block** serialised after the tree root (`u16 count` + per-entry `{ name,
>   serialised query }`, the query an opaque `ui::AdvancedQuery` blob), bumping
>   `INDEX_VERSION` to **5**; pre-v5 blobs read with an empty saved-searches list.
>   The block is not part of any node — it is vault-level metadata, persisted via
>   the same crash-safe double-buffer index swap and bounded against the fuzz suite.
> - **Phase 37 (Persisted sort order):** ✅ shipped — a `sort_key u8` on every
>   Gallery node (`INDEX_VERSION = 6`; v1–v5 blobs read as `Manual` — no visible
>   change until a user opts in via `Shift+S`). An out-of-range byte is
>   rejected on deserialise, not clamped.
> - **Phase 47 (Animated GIFs):** ✅ shipped — an `animated u8` flag on every
>   Image node, written after `thumb_length` (`INDEX_VERSION = 7`; v1–v6 blobs
>   read as not animated and are healed lazily on first view). A byte other
>   than 0/1 is rejected on deserialise, not clamped.
> - **Phase 49 (Tag chips & per-vault settings):** ⬜ planned — a **vault-global
>   settings block** serialised after the saved-searches block (`default_sort u8`,
>   `tiles_show_tags u8`, then `u16 count` + per-entry `{ name, swatch u8 }` tag
>   categories), bumping `INDEX_VERSION` to **8**; pre-v8 blobs read with the
>   default sort `Insertion`, tile tags on, and the built-in category seed. The
>   same bump re-reads a Gallery's `sort_key` byte 0 as `Default` ("follow the
>   vault default") and adds `7 = Insertion` for raw import order, so existing
>   galleries adopt the vault default with no migration. Out-of-range
>   swatch/sort/flag bytes are rejected on deserialise, not clamped.
