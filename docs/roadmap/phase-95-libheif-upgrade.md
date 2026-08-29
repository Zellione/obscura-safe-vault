# libheif 1.18.2 → 1.23.2 upgrade (Phase 95)

**Status:** 🔜 ready for review
**Date:** 2026-08-29

This is **Phase B** of the 2026-08-29 security audit remediation
(`AUDIT.md` / `AUDIT_IMPLEMENTATION.md`, phases A–G mapping to app phases
94–100). It fixes **OSV-AUD-002** (High — pinned libheif sits inside the
affected ranges of multiple known decoder vulnerabilities). No `.osv` byte
change; the C API surface used by `decode_heif_from_memory` is untouched.

## Findings being fixed

**OSV-AUD-002 — libheif 1.18.2 has known decoder vulnerabilities.** The pin
was inside the published affected ranges for:

- **GHSA-9h96-c44j-jpq9** — heap stride-integer-overflow → undersized plane
  allocations and later writes (affected ≤ 1.19.8).
- **GHSA-2vh6-whr3-cmq3** — heap information disclosure through
  uninitialized grid-image pixels (affected ≤ 1.21.2).
- **GHSA-hg7q-rjr2-8x46** — heap out-of-bounds reads during overlay
  compositing (affected ≤ 1.21.2).

A crafted HEIC/AVIF selected for import can crash the process, read adjacent
heap into pixels, or corrupt heap — in a process already holding unlocked
master keys and decrypted media.

**Version choice note:** the audit text named 1.23.1 as "the newest
compatible", but **v1.23.2** published 2026-08-25 (4 days before the audit)
is a security release fixing **two critical** CVEs (GHSA-g89c heap overflow
in `scale_nearest_neighbor` via duplicate alpha planes from nested
iden/auxl items; GHSA-2jg2 out-of-bounds read/write in derived-item and
pixel-plane handling — a confirmed code-execution exploit) plus five
high/medium advisories; both critical fixes are absent from 1.23.1. Per the
audit's own "prefer the newest stable release" rule the pin is **v1.23.2**
(commit `ac1cb05c39008f01525c991ff8b88f84ddf70fd2`), a stated drop-in,
ABI/API-compatible with 1.23.1.

## What shipped

### Submodule bump

`vendor/libheif` re-pointed from tag `v1.18.2` to **`v1.23.2`**. libde265
(1.0.15) and libaom (3.14.1) are unchanged; both satisfy the new libheif's
`find_package` minimums (verified at build time — the cmake summary resolves
both against `vendor/codecs-prefix` and `libheif.a` carries the expected 21
`de265_*` / 8 `aom_*` undefined-symbol references).

### Build script changes (required by the 1.23.x surface)

`scripts/build_codecs.sh` and `scripts/build_codecs.bat` — the libheif line
now passes the previously-unneeded flags:

```
-DWITH_X264=OFF -DWITH_OpenH264_DECODER=OFF
```

libheif ≥ 1.19 added the `plugin_option` mechanism and builds at **C++20**;
`WITH_X264` and `WITH_OpenH264_DECODER` default **ON**, so without these two
flags libheif's `find_package` would pull in a system AVC encoder/decoder
detected via `CMAKE_PREFIX_PATH` — expanding both attack surface and linked
code — regardless of the decode-only intent (the newly-defaulted-on encoders
SvtEnc/RAV1E/OpenJPEG/OPENJPH and the dav1d decoder default OFF and are not
touched). `WITH_LIBDE265=ON`, `WITH_AOM_DECODER=ON`,
`ENABLE_PLUGIN_LOADING=OFF` are unchanged; the decoders stay statically
baked in and both static plugins register at first API use (no
`heif_init()` call is needed — confirmed by the fixture tests at runtime).

**CI cache note (important):** the codec cache key hashes `.gitmodules` +
`scripts/build_codecs.{sh,bat}`, so the script edits automatically bust it —
no stale 1.18.2 `libheif.a` can be cached. Local rebuild deleted the
`libheif.a`/headers/pkgconfig/cmake-config install products and the CMake
build dir so nothing idempotent-skipped.

### Source

`src/image/decode_heif.cpp` compiled **unchanged** — every function it uses
(`heif_context_alloc/free`, `heif_context_read_from_memory_without_copy`,
`heif_context_get_primary_image_handle`, `heif_decode_image`,
`heif_image_get_width/height`, `heif_image_get_plane_readonly`,
`heif_image_release`, `heif_image_handle_release`) is present with identical
signatures in 1.23.2's public C API. The 1.23.2 hardening — C++ exceptions
such as `std::bad_alloc` can no longer escape the C API read/decode entry
points (returned as `heif_error` instead of aborting) — additionally matches
this project's no-exceptions policy.

## Tests

Two committed fixtures + two decode regressions for the formerly-vulnerable
paths (the vendored libheif is decode-only, so the fixtures were generated
with system tooling):

- **`tests/image/fixtures/sample_grid.avif`** — 128×128 **2×2 grid** (four
  64×64 cells: red / green / blue / white). Exercises the GHSA-2vh6
  grid-image pixel-initialization path.
  `decode_avif_grid_reassembles_cells_with_initialized_pixels` asserts the
  fully assembled 128×128 output and that each quadrant's pixels are the
  expected solid colour at the expected position (never stale or
  uninitialized bytes).
- **`tests/image/fixtures/sample_overlay.heic`** — 64×48 canvas with a
  40×24 `#3366cc` child at (0,0), a 16×16 `#cc6633` child at (8,8), opaque
  black background. Exercises the GHSA-hg7q overlay-compositing path.
  `decode_heic_overlay_composites_children_in_order` asserts bottom-only,
  top-over-bottom, and background regions.

Colour assertions are intentionally tolerant (±3 LSB / relational): even
lossless HEVC/AV1 round-trips through a YUV coloursapce frame, so channel
bits can shift a couple of LSBs (measured: the AV1 grid's saturated green
chroma sub-samples to 0x80). Generation commands + provenance are in
`tests/image/fixtures/README.md`.

Existing coverage retained and green against the new lib: valid-decode tests
(`decode_heic_format_and_dims`, `decode_avif_format_and_dims`),
`decode_malformed_heif_returns_nullopt`, and the 500-mutation
`fuzz_heic_survives_500_malformed_inputs` / `fuzz_avif_survives_500_malformed_inputs`
(which run under the ASAN-instrumented codecs on the `tests-asan-codecs` CI
leg).

2209 tests / 0 failed (baseline 2207 + 2); ASAN clean.

## Deliberately unchanged

- **No `.osv` byte change, `INDEX_VERSION` stays 12.**
- **libde265 1.0.15 and libaom 3.14.1 are not bumped** — both satisfy the
  new libheif's minimums and the audit scopes OSV-AUD-002 to libheif itself.
- **FFmpeg and every other vendored codec are untouched**; the surgical
  rebuild removed only libheif's install/build products, not the shared
  prefix.
- **`decode_heif_from_memory` is source-identical** to the 1.18.2 era.

## Memory graph updates

- `mem:tech_stack` — libheif row: 1.18.2 → 1.23.2, C++20 build requirement,
  the two disabled `WITH_*` codec flags, and the fixture additions.
- `docs/VENDORED_DEPS.md` — libheif pin, an advisory-by-advisory review
  table, the build-surface note, and a corrected libde265 tag (the previous
  `v0.1-2267-g17bb8d9f` describe string is stale; the submodule is at tag
  `v1.0.15`, commit `17bb8d9f`).

## Real-file compatibility

Representative real-world HEIC/AVIF passes are an owner-side check (the
fixtures cover both grid and overlay constructs plus fuzz; the `running-the-app`
skill can drive a GUI import smoke test). CI covers Linux + Windows + the
ASAN-codecs fuzz leg.