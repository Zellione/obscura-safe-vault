# Phase 121 — Multipart, solid and encrypted archive import

Status: implemented; owner merge pending. Validation results are recorded in the PR.

## Findings

Selecting several archive volumes bypassed the single-file picker's set
detection and queued every fragment independently. Worker counts were also
copied into the displayed imported count before attachment incremented it again.
The worker's local task discarded the supplied archive password.

Separately, the libarchive build cannot decrypt 7z/RAR and rejects older solid
RAR. Those were decoder limitations, not just selection failures. Upstream
fixtures reproduced both failures before the new decoder was connected.

## Changes

Bulk selection groups complete sibling volume sets, deduplicates selected parts,
and retains confirmation/name prompts and missing-volume refusal. Cancelling one
set advances to the next deferred pick. Imported counts represent successful
attachment only, with distinct worker and attachment skips added.

7z/RAR now use an in-process, extraction-only 7-Zip decoder supporting solid
compression, encrypted contents and encrypted filenames. The picker probes full
sets for encryption and uses the existing masked password prompt. Passwords move
into worker ownership and are wiped after use. Wrong/missing passwords and
corrupt selected-archive data fail visibly instead of showing a successful
zero-file import.

The reader pauses one forward decompression pass between requested entries,
checks each extraction result before releasing bytes, and joins its producer on
rewind/destruction. Private decoder heap mappings are locked where possible and
wiped on release; global password/key caches are disabled. Opaque stack scratch
is documented and reflected in F1's degraded memory status. See
[decoder architecture, memory limits and licensing](../ARCHIVE_DECODER.md).

## Validation

Regression coverage includes bulk multipart selection, sibling discovery, gaps,
actual confirmation/name/password dialogs, v3 imports with cold-reopen byte
comparison, wrong/missing passwords, solid encrypted RAR4/5, encrypted 7z headers,
corrupted ciphertext, Unicode passwords, split solid 7z, sequential/rewind reads,
and decoder teardown without retained key-cache allocations.

Full Debug, Release, no-FFmpeg, ASAN/UBSan, CI and SonarCloud results are recorded
in the PR before handoff. The owner merges.
