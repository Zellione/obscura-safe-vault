# Vendored Dependencies

This document tracks the pinned versions of all vendored third-party libraries and their security posture.

## Dependency Table

| Submodule | Pinned Version | Role | Parses Untrusted Input |
|---|---|---|---|
| **SDL3** | 3.4.10 | Windowing, rendering, input, file dialogs | No |
| **monocypher** | 4.0.2 (0d85f98) | AEAD cipher (XChaCha20-Poly1305), KDF (Argon2id), cryptographic utilities | No |
| **stb** | master | Header-only image decode (JPEG/PNG/GIF/BMP), text rendering | **Yes** |
| **miniz** | e78dfd2 (master) | ZIP archive decompression | **Yes** |
| **libwebp** | 1.4.0 | WebP image decoding (decode-only) | **Yes** |
| **libde265** | 1.0.15 (v1.0.15, 17bb8d9f) | HEVC video codec (decode-only, internal to libheif) | **Yes** |
| **libaom** | 3.14.1 | AV1 video codec (decode-only); AVIF stills via libheif, and (Phase 40) linked a second time into FFmpeg as its `libaom-av1` decoder for AV1 video | **Yes** |
| **libheif** | 1.23.2 | HEIF container format parser (HEIC/AVIF) | **Yes** |
| **FFmpeg** | 7.1.1 (n7.1.1) | Video and audio frame decoding (decode-only, static linked; H.264/H.265 + ProRes/DNxHD/MJPEG since Phase 28; VP8/VP9 since Phase 38; AV1 via libaom + QTRLE/Cinepak since Phase 40; GIF demuxer + decoder since Phase 47; legacy containers AVI/MPEG-PS/MPEG-TS/ASF/FLV/Ogg/RealMedia + ~34 legacy codecs since Phase 52; libavfilter linked for yadif deinterlacing since Phase 52; VAAPI/D3D11VA hwaccels enabled since Phase 52) | **Yes** |
| **nlohmann/json** | v3.12.0 | Header-only JSON parsing (archive `meta.json`, Phase 27) | **Yes** |
| **zlib** | 1.3.2 | gzip filter for libarchive (`.tar.gz`) | **Yes** |
| **xz / liblzma** | 5.8.3 | LZMA2 filter for libarchive (`.7z`, `.txz`) | **Yes** |
| **libarchive** | 3.8.8 | 7z/RAR/TAR archive read (decode-only; Phase 34) | **Yes** |
| **libva** | 2.22.0 (217da1c) | VA-API public headers only (Linux hardware video decode, Phase 43 Part 2) — never built; `vendor/vaapi-shim` dlopens the real `libva.so.2`/`libva-drm.so.2` at runtime instead of linking them | No |

### Decode-Only Rationale

FFmpeg, libheif, libaom, libde265, libwebp, and stb are all compiled in **decode-only mode**:

- **FFmpeg** (`vendor/ffmpeg`, `scripts/build_codecs.sh`): Built with `--disable-encoders --disable-muxers --disable-network` and gated by `OSV_VENDORED_AV`. No frame encoding, container writing, or network protocol support compiled in. Only audio/video frame decoders, demuxers, and basic format probing.
- **libheif, libaom, libde265** (`vendor/libde265`, `vendor/libaom`, `vendor/libheif`, compiled via `scripts/build_codecs.sh`): Decoders only; no encoders or format-specific I/O.
- **libwebp** (`vendor/libwebp`): Decoder library; no WebP encoder.
- **stb** image and text modules (`vendor/stb`): Decode-only; no encoding.

This minimizes attack surface: **untrusted input enters only through image/video/ZIP data streams that users explicitly load**, never through configuration, container metadata, or network sources.

## Quarterly CVE Review Cadence

The libraries marked "**Yes**" in the "Parses Untrusted Input" column are reviewed quarterly for known CVEs:

**Affected libraries:** stb, miniz, libwebp, libde265, libaom, libheif, FFmpeg, nlohmann/json, zlib, xz/liblzma, libarchive

**Review schedule:** Every 3 months (or upon public disclosure of a critical issue)

**Check command:**
```bash
# For each of the above submodules:
cd vendor/<name>
git fetch --tags origin
git log HEAD..origin/HEAD --oneline  # Show commits not yet merged
```

Then cross-reference against:
- **NVD (National Vulnerability Database):** https://nvd.nist.gov/ — search `<library> <version>` e.g. "libwebp 1.4.0"
- **Project security advisories:**
  - FFmpeg: https://ffmpeg.org/security.html
  - libheif: https://github.com/strukturag/libheif/security/advisories
  - libwebp: https://github.com/webmproject/libwebp/security/advisories
  - libaom: https://github.com/aomedia/av1-codec/security/advisories
  - stb: https://github.com/nothings/stb/issues (no formal advisory system; check closed security reports)
  - libarchive: https://github.com/libarchive/libarchive/security/advisories
  - zlib: https://github.com/madler/zlib/security/advisories
  - xz/liblzma: https://github.com/tukaani-project/xz/security/advisories (note: unrelated to the 2024 XZ Utils backdoor, CVE-2024-3094, which targeted the `liblzma` build's injected `.so` — we build a static lib from source with no such injected artifact)

If new CVEs are discovered, follow the bump procedure (see below).

### libheif advisory review (Phase 95 / OSV-AUD-002, reviewed 2026-08-29)

Target of the upgrade: **1.18.2 → 1.23.2**. Every libheif advisory published
after 1.18.2 was checked against the project's enabled decode path
(HEIC via vendored libde265, AVIF via vendored libaom, plugin loading OFF,
decode-only, no encoders). 1.23.2 fixes all reviewed advisories and is
ABI/API-compatible with 1.23.1:

| Advisory | Affected ≤ | Component | Fixed by |
|---|---|---|---|
| GHSA-9h96-c44j-jpq9 (heap stride-integer-overflow, undersized plane allocs) | 1.19.8 | HEVC/AV1 plane allocation | 1.19.x |
| GHSA-2vh6-whr3-cmq3 (heap info disclosure via uninitialized grid pixels) | 1.21.2 | grid-image decode | 1.21.2+ |
| GHSA-hg7q-rjr2-8x46 (heap OOB reads in overlay compositing) | 1.21.2 | overlay (iovl) composite | 1.21.2+ |
| GHSA-g89c-p67h-r497 (heap overflow in `scale_nearest_neighbor`, duplicate alpha planes; **critical**) | 1.23.1 | derived-item decode | **1.23.2** |
| GHSA-2jg2-4ch7-h545 (OOB R/W in derived-item/pixel-plane handling; **critical**, confirmed code-exec) | 1.23.1 | iden/auxl chains | **1.23.2** |
| GHSA-24wx-9w62-c96w (brotli/zlib decompression bomb in mime/unci) | 1.23.1 | decompression limits | **1.23.2** |
| GHSA-x8xm-cm2c-cfc8 (CPU/memory amplification via derived-image ref chains) | 1.23.1 | decode caching/limits | **1.23.2** |
| GHSA-xw34-mjcp-jqh8 (non-terminating decode loop via sequence sample timing) | 1.23.1 | sequence decode | **1.23.2** |
| GHSA-j264-xvrp-5v7q (OOB write in unci encoder) | 1.23.1 | unci encoder — **not shipped** (decode-only) | — |
| GHSA-p58j-h3vm-3fp5 (heap OOB read in inline-mask region API) | 1.23.1 | region API — not exercised by Decoder | **1.23.2** |

**Build changes required by the 1.23.x surface:** libheif now builds at
**C++20** and gained a `plugin_option` mechanism where `WITH_X264` and
`WITH_OpenH264_DECODER` default **ON** — both must stay **OFF** in
`scripts/build_codecs.{sh,bat}` or libheif pulls in an unwanted AVC
encoder/decoder (Phase 95). The decoder-only build keeps `WITH_LIBDE265=ON`,
`WITH_AOM_DECODER=ON`, `ENABLE_PLUGIN_LOADING=OFF` — libde265 and libaom stay
statically baked in. The 1.23.2 hardening "C++ exceptions cannot escape the C
API read/decode entry points" also matches this project's no-exceptions policy.

Transitive compatibility confirmed at build time: vendored **libde265 1.0.15**
and **libaom 3.14.1** both satisfy 1.23.2's `find_package` minimums and stay
linked statically into `libheif.a` (verified by undefined-symbol scan).

## How to Bump a Dependency

1. **Identify the new tag/commit:**
   ```bash
   cd vendor/<submodule>
   git fetch --tags origin
   git log origin/HEAD --oneline -10  # See recent commits/tags
   ```

2. **Update the submodule to the new pin:**
   ```bash
   cd /path/to/obscura-safe-vault
   git submodule set-url vendor/<submodule> <new-remote> # if remote changed
   git -C vendor/<submodule> checkout <new-tag-or-commit>
   git add vendor/<submodule>
   ```

3. **Rebuild and test:**
   ```bash
   scripts/build_codecs.sh       # For codec libraries (if applicable)
   scripts/test.sh               # Full test suite (debug)
   scripts/test.sh --asan        # AddressSanitizer + UBSan
   scripts/test.sh --release     # Release build tests
   ```
   Crypto KAT tests in `tests/crypto/` are the primary guard; they will fail if cipher/KDF behavior changes.

   **FFmpeg bumps additionally require the swscale canary sweep** (Phase 80 —
   ASAN cannot see stores made by vendored SIMD asm, so the suite alone does
   not cover this class):
   ```bash
   gcc scripts/swscale_canary_sweep.c -I vendor/codecs-prefix/include \
       -Wl,--start-group vendor/codecs-prefix/lib/libswscale.a \
       vendor/codecs-prefix/lib/libavutil.a -Wl,--end-group \
       -lm -lva -lva-drm -o /tmp/sws_sweep
   /tmp/sws_sweep padded   # MUST print "0 overshooting case(s)"
   ```
   If `padded` mode ever reports overshoots, the new libswscale writes further
   past row ends than the codebase's padding contract (`FFALIGN(w*bpp, 64)`
   linesize + 128-byte tail — see AGENTS.md § Hardening notes) absorbs; widen
   the contract at every `sws_scale` site in `src/media/` before shipping.

4. **Inspect the delta:**
   ```bash
   git -C vendor/<submodule> log <old-tag>..<new-tag> --oneline
   git -C vendor/<submodule> diff <old-tag>..<new-tag> -- src/  # Source changes only
   ```
   Ensure changes are security fixes, not feature additions that expand attack surface.

5. **Commit:**
   ```bash
   git commit -m "Bump <submodule> to <new-version>

   Security: <brief reason: CVE fix, compatibility, etc.>
   
   Tested: scripts/test.sh, scripts/test.sh --asan, scripts/test.sh --release
   "
   ```

## Notes

- **monocypher** is pinned to **4.0.2** (0d85f98 release tag). Upstream tag 4.0.3 exists with constant-time hardening fixes (fe_ccopy volatile mask, fe_cswap fix, loop-unroll mitigation) — upgrade is an owner decision.
- **miniz** and **stb** are pinned to `master` branches of their respective repositories, not stable release tags. Both are stable, widely-used libraries with infrequent breaking changes. Monitor for updates every 6 months.
- **SDL3** is the only windowing/platform layer; it is NOT an untrusted-input parser.
- **libva** is vendored **headers-only** and never built: `vendor/vaapi-shim`
  (Phase 43 Part 2) uses its headers to compile a `dlopen`-based forwarding
  shim, so the app never links or requires real libva at build time. See
  `docs/superpowers/specs/2026-07-17-hardware-video-decode-design.md`.

## Related

See [AGENTS.md § Dependency management](../AGENTS.md) for vendoring strategy and build instructions.
