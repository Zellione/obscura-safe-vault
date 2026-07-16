## Phase 35 — Password-protected archive import (ZIP/CBZ) ✅

**Goal:** Let the user import a password-protected `.zip`/`.cbz` archive by
prompting for the **archive's** password at import time. Distinct from, and
never stored alongside, the vault's own password.

**Scope note (rescoped from the original plan):** investigating the vendored
libarchive 3.8.8 reader source before implementation found that its 7z reader
(`archive_read_support_format_7zip.c`) and RAR/RAR5 readers
(`archive_read_support_format_rar.c`/`_rar5.c`) hard-code encrypted content as
unsupported — unconditionally, in any build configuration, not something more
code can fix. WinZip-AES-encrypted zip needs a crypto backend
(OpenSSL/mbedTLS/etc.) that this project doesn't currently vendor
(`scripts/build_codecs.sh`'s libarchive build passes
`-DENABLE_OPENSSL=OFF -DENABLE_MBEDTLS=OFF` etc.). Only ZIP's traditional
("ZipCrypto") encryption is self-contained in libarchive and achievable
without a new dependency. This phase covers that case only; 7z/RAR/AES-zip
password prompting is out of scope (see Tasks below) — picking one of those
still gets today's plain "could not open/import" error, never an
unsatisfiable password prompt.

### Tasks
- [x] **Detection** — `ui::zip_is_encrypted(path)` (miniz-based, mirrors
  `peek_archive_meta`) checks `mz_zip_archive_file_stat::m_is_encrypted`
  across a zip's entries at file-pick time. An encrypted `.zip`/`.cbz` is
  forced through the libarchive backend (`ArchiveReader`) instead of miniz —
  miniz has no decrypt path for any encryption flavor.
- [x] **Password-prompt modal** — masked input (`ui::SecureTextField`,
  mirroring the vault-unlock field) wired into `ZipImportJob`/`GalleryGrid`:
  a `ZipImportOutcome::needs_password` outcome (returned by a verification
  probe in `import_archive`/`import_archive_cbz` before any vault write)
  pauses the import and prompts, reusing the exact same deferred-outcome
  modal pattern as the existing Flatten/Skip mixed-folder case.
- [x] **Wrong-password handling** — a clear inline error ("Incorrect
  passphrase"), re-prompt or cancel; the verification probe runs before any
  gallery is created or any chunk is written, so a wrong password leaves the
  vault completely untouched.
- [x] **Secret hygiene** — the typed archive password lives only in a
  short-lived mlock'd buffer for the import job's duration and is
  `crypto_wipe`'d on completion or cancel (invariant #2 applies to this
  secret too, even though it isn't the vault password). Disclosed exception:
  `archive_read_add_passphrase` internally `strdup()`s the passphrase into an
  unmanaged buffer for the life of the `archive*` handle — a limitation of
  treating libarchive as a black box, not something this codebase controls;
  that handle's lifetime is already bounded to a single import attempt.
- [x] Update `CLAUDE.md` / `mem:core`.
- [x] `tests/` — a ZipCrypto-encrypted zip/cbz fixture (built via libarchive's
  own writer, `zip:encryption=traditional`) requires the password prompt and
  imports correctly with the right passphrase; a wrong passphrase is
  rejected with no partial write; cancelling the prompt leaves the vault
  untouched; a plain (non-encrypted) 7z/rar/tar import behaves identically to
  before (no new prompt).

**Out of scope (YAGNI / infeasible):** WinZip-AES-encrypted zip, encrypted
7z, encrypted RAR/CBR/CB7 (see the scope note above); saving/remembering
archive passwords anywhere (`VaultRegistry` stores no secrets — unchanged);
brute-forcing/recovering a lost archive password; encrypted-filename
("header encryption") zip variants (traditional zip encryption never
encrypts filenames, so this doesn't arise in scope). Detection can't tell
ZIP encryption flavors apart before the password is checked, so a WinZip-AES
zip does get one password prompt like a traditional-encryption one would —
but any password against it resolves to a plain generic import error rather
than a re-prompt, so it can never become an infinite unsatisfiable loop.

### Acceptance criterion
Importing a password-protected `.zip`/`.cbz` archive (traditional/ZipCrypto
encryption) prompts for its password, imports correctly once given, rejects a
wrong password with a clear inline error and no partial write, and never
persists the archive password anywhere. Picking an encrypted 7z or encrypted
RAR/CBR/CB7 behaves exactly as it does today (no prompt). Picking a
WinZip-AES-encrypted zip shows one password prompt (indistinguishable from
traditional encryption at pick time) but never loops — any password attempt
resolves to a plain generic import error.

**Status:** ✅ Done. Built and tested on Linux (Debug + `--asan`, 0 leaks/UB).
