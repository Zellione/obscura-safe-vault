# Phase 121 — Multipart archive selection and import counts

Status: implemented; owner merge pending.

## Confirmed failures

The single-file picker path detected multipart archives, but selecting several
files bypassed detection and queued every volume as an independent archive.
Selecting `photos.7z.001` and `photos.7z.002`, or several `book.partN.rar` files,
therefore attempted to read incomplete fragments instead of the complete set.

Archive workers also copied their staged-media total into the imported counter.
The main-thread drain subsequently incremented it again when attaching queued
media. A one-image archive could report two imports, and a destination collision
could still be reported as an import. Worker-side and attachment-side skips were
merged with `max`, which could lose part of the skipped count.

## Behavior

- Bulk selection groups volume sets using sibling listings, including parts not
  explicitly selected. Each set appears once, in first-selection order; equal
  filenames in different directories remain separate.
- Every detected set uses the existing confirmation and gallery-name prompts.
  Missing-volume gaps block confirmation. Cancelling a set advances to the next
  deferred archive. Ordinary unencrypted single archives retain immediate queuing.
- The existing worker assembly routes remain responsible for concatenated 7z,
  file-oriented RAR, and spanned ZIP input. No external extraction process or
  temporary plaintext file is introduced.
- Imported counts come exclusively from successful main-thread attachment.
  Worker skips and attachment collision skips are added because they represent
  disjoint entries.

## Coverage and limits

Regression tests cover grouped 7z/RAR picks, unselected sibling discovery,
directory separation, missing-volume refusal, the actual picker-to-dialog-to-queue
flow, and single/split 7z imports into a v3 vault with byte comparison after cold
reopen. Reimporting the same media verifies collision and skipped counts.

The existing single-archive and multipart RAR reader tests pass. A general
single-file 7z/RAR failure has not been reproduced. These changes do not extend
libarchive's decoder support: encrypted 7z/RAR and older solid RAR remain
limitations of the current reader. Diagnosing a different failure requires its
error text and archive characteristics.

Debug and Release each pass all 2,488 tests. The Serena memory audit reports no
referential integrity issues. The no-FFmpeg build/test run and PR CI/SonarCloud
checks are also required before handoff.
