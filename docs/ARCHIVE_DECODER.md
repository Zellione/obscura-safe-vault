# In-process 7z and RAR decoder

Phase 121 routes 7z, RAR4 and RAR5 to a private extraction-only build of
[7-Zip 26.04](https://github.com/ip7z/7zip/tree/26.04), pinned at
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. TAR keeps libarchive, ordinary
ZIP/CBZ keeps miniz, and password-protected ZIP uses the existing libarchive
path (ZipCrypto; WinZip AES remains unsupported by that build).

## Behavior and limits

- Supports solid compression, encrypted contents and encrypted headers/names in
  7z and RAR4/5. One password is supplied per import; nested archives using a
  different password are skipped. A wrong password in the selected archive
  fails the task; previously imported valid entries can remain.
- Numeric 7z volumes are concatenated in set order. The reader's file API also
  provides a seekable concatenation for header probing. RAR callbacks only open
  the explicitly supplied ordered paths, never archive-controlled filenames.
- Extraction uses one producer thread and one outstanding requested file. The
  decoder pauses between requests, retaining its solid dictionary. Ascending
  requests take one pass; backward requests cancel/join and restart. No external
  command is executed and no plaintext extraction directory is created.
- File bytes become available only after the decoder reports success and the
  declared size matches. Password and damaged-encrypted-data failures may be
  indistinguishable in older formats, so the error says both. CRC verification
  is not a cryptographic authenticity guarantee for archive formats.
- Limits: 100,000 entries, 4 GiB per extracted entry, and 1 GiB aggregate private
  decoder mappings. Existing recursive depth/byte budgets still apply. Some
  archives with very large dictionaries therefore fail with a memory-limit
  message. Nested archives inside multipart RAR retain the flat import behavior.

## Memory ownership

`vendor/7zip-shim` builds only 7z/RAR handlers and their extraction codecs. The
private shared library's C and C++ heap allocations are linker-wrapped into
zeroed anonymous mappings, best-effort `mlock`/`MADV_DONTDUMP`, and Monocypher
wipe-before-unmap. Failed realloc preserves the original buffer. This does not
interpose allocation in the application or other libraries. The ASAN build
instruments the decoder and explicitly poisons mapping tail padding, since
anonymous mappings bypass ASAN's normal malloc interceptor.

The private build selects upstream portable byte loads/stores instead of x86
unaligned integer-pointer casts, preserving UBSan alignment checks. It also
removes upstream process-global 7z AES and RAR5 key caches;
per-reader caches die with their decoder. Passwords use SecureBytes and direct
secure BSTR allocation, without an ordinary string copy. ArchiveReader source
copies and extracted output are also SecureBytes. Teardown joins the producer
before destroying its input, output or password.

Opaque SDK stack scratch remains outside the heap allocator's control. Opening
this decoder marks `opaque_plaintext_seen()`; F1 therefore reports the existing
degraded secure-memory status. Lock failures propagate the separate process-wide
mlock warning/status. This is a documented boundary, not a claim that every
third-party transient is locked or wiped.

## Build, license and replacement

Run `scripts/build_7zip.sh` with CMake, Ninja and a C/C++ compiler installed.
`--asan` and `--tsan` use separate build/install prefixes. The script is included
in setup/codecs, build and test workflows. The library is copied beside each
binary; `$ORIGIN` resolves it there. No system 7z executable is required.

7-Zip is distributed under LGPL 2.1 or later with the upstream unRAR restriction
and additional notices described in `vendor/7zip/DOC/License.txt`,
`copying.txt`, and `unRarLicense.txt`. The restriction prohibits using the RAR
source to develop a RAR-compatible compressor. This build contains no RAR writer.
Monocypher's license is included alongside its source.

Release packages contain the replaceable `libosv7zip.so` and complete
corresponding decoder sources under `decoder-source/`: upstream C/CPP/DOC,
our CMake adaptations and allocator, Monocypher, and the build script. From
that directory run `scripts/build_7zip.sh`; the result is
`vendor/codecs-prefix/lib/libosv7zip.so`. Replace the copy beside `osv` with your
rebuilt library. The package installer retains these sources under
`$PREFIX/share/osv/decoder-source`. Cache-removal adaptations are applied only
to the private build tree and fail configuration if the pinned source changes.
