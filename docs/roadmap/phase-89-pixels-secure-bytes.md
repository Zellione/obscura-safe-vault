# Decoded pixels were a plain swappable `std::vector` (Phase 89)

**Status:** 🔜 ready for review
**Date:** 2026-08-26

## Problem

Phase 4 of the break-in/hardening effort (`docs/break-in-effort.md`) measured
the largest plaintext blob the app holds: a decoded photo is 26–36 MB of
RGB, and it was a **plain `std::vector<uint8_t>`** (`image.h`) — never
page-locked, never explicitly wiped, freed back to the allocator on
destruction. The "no plaintext to disk" invariant therefore held for it only
by the luck of this host's zram-only swap configuration, and the
degrade-to-swappable path was *invisible*: there was no state to query and no
mark distinguishing "this buffer is best-effort locked" from "this buffer is
just a vector". The AGENTS.md hardening note ("Pixel data is best-effort
mlock'd") described the encrypted stored bytes only.

## What shipped

- **`ImageData::pixels` is now a `crypto::SecureBytes`** (src/image/image.h) —
  move-only, mlock'd best-effort (+ `MADV_DONTDUMP` on Linux) and
  `crypto_wipe`'d on destruction, exactly like the encrypted stored bytes.
  Both draw on the same 256 MiB budget, so a large full-res decode after
  reading chunks can legitimately exhaust it and degrade *that one* buffer to
  swappable — with the existing warn-once log and a queryable
  `is_locked()`, instead of silently living in swappable memory unmarked.
- **`SecureBytes` gained the vector ergonomics the decode path needs**
  (src/crypto/secure_mem.h): `operator[]` (unchecked — the buffer is sized
  exactly for its contents), `assign(std::span<const uint8_t>)`, and
  `fill(size_t, uint8_t)`. All `[[nodiscard]]`; OOM returns `false` and leaves
  the object empty (allocation failure used to be a throw, so every call site
  now handles the non-throwing failure explicitly).
- **Every decode site handles the allocation failure with `nullopt`:**
  `decode.cpp` (stb), `decode_webp.cpp` (static + animated first frame),
  `decode_heif.cpp`, and `video_decoder.cpp`'s poster path.

## Tests

- `secure_bytes_operator_index_read_write`, `secure_bytes_assign_copies_source`,
  `secure_bytes_assign_replaces_previous_contents`
  (tests/crypto/test_secure_bytes.cpp).
- `decoded_pixels_are_mlocked` (tests/image/test_image.cpp) — a decoded JPEG's
  pixels report `is_locked() == true`, pinning the new invariant for the whole
  decode pipeline.

2138 tests / 0 failed (baseline 2134 + 4); ASAN clean; TSan clean — the only
reported race is the known local `radeonsi_drv_video.so` driver race (Phase 42
issue, absent on CI runners which have no VA-API driver), byte-identical in a
clean-tree baseline.

## Deliberately unchanged

- The zero-init of allocations (`std::make_unique<uint8_t[]>` value-initialises)
  — consistent with the existing "stb_image allocations are zero-initialised"
  hardening note.
- The 256 MiB budget and the warn-once + degrade-to-swappable contract —
  Phase 89 makes the *state* explicit, it does not change the policy.
- The `.osv` format and `INDEX_VERSION` — a buffer-type change is invisible to
  the container.
