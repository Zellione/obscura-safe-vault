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
| **libde265** | v0.1-2267-g17bb8d9f | HEVC video codec (decode-only, internal to libheif) | **Yes** |
| **libaom** | 3.14.1 | AV1 video codec (decode-only); AVIF stills via libheif, and (Phase 40) linked a second time into FFmpeg as its `libaom-av1` decoder for AV1 video | **Yes** |
| **libheif** | 1.18.2 | HEIF container format parser (HEIC/AVIF) | **Yes** |
| **FFmpeg** | 7.1.1 (n7.1.1) | Video and audio frame decoding (decode-only, static linked; H.264/H.265 + ProRes/DNxHD/MJPEG since Phase 28; VP8/VP9 since Phase 38; AV1 via libaom + QTRLE/Cinepak since Phase 40) | **Yes** |
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

See [CLAUDE.md § Dependency management](../CLAUDE.md) for vendoring strategy and build instructions.
