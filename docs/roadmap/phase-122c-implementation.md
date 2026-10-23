# Phase 122C — Regex bulk rename implementation

This isolated implementation is based on `7e5ccd9` (122B), for stacking after
category rename. It does not update the central roadmap, README or Serena.

## Behavior

Select gallery/image/video tiles with Space (or Ctrl+A), then **Shift+R**.
Plain **R** retains the existing single-item rename. Shift+R is listed in F1.
The bulk dialog holds import exclusivity until closed. Tab switches between
pattern and replacement. Enter computes a preview; a second Enter applies that
exact valid preview. Editing either field or an option invalidates confirmation.
Escape cancels the dialog or requests cancellation of preparation. Once the final
commit starts, cancellation is disabled. Closing/joining precedes vault teardown.

Options (also shown in the dialog):

- F3: preserve the last media extension, **on by default**; gallery names are
  always matched in full. A leading dot alone does not count as an extension.
- F4: recurse into selected galleries, **off by default**. Selected parent/child
  overlap is deduplicated by stable node ID.
- F5: case-sensitive matching, **on by default**.
- F6: all matches, **off by default** (first match only).
- PgUp/PgDn: scroll through preview rows, including unchanged/unmatched rows.
- F7: show the current row's full old/new names, wrapped instead of elided.

Dialect: PCRE2 UTF-8 with Unicode properties. Replacements support `$1`, `$2`,
`${name}` captures and `$$` for a literal dollar. Unknown/unset captures are
errors, not silent empty strings. Examples: `^(.*)-([0-9]+)$` with `$2-$1`
turns `trip-12.jpg` into `12-trip.jpg` with extension preservation. `^` with
`holiday-` prefixes names. Empty patterns are rejected. Literal path separators,
reserved/empty/oversized results, malformed UTF-8 and regex limits produce
invalid rows. Unsafe results are never silently sanitized.

Preview uses secure strings for options, original names and resulting names.
Regex allocations are best-effort locked and always wiped. It does not write
plaintext sidecars or temporary names to the filesystem. Rendering uses the
existing UI text pipeline. Copy/cut from the secure pattern fields is disabled;
paste is supported through existing text-input handling.

## Vault API and atomicity

`vault::preview_rename(Vault&, span<RenameId>, RenameOptions, OpProgress*)`
returns `RenamePreview` with stable target/parent IDs, before/after names and
per-row Changed/Unchanged/Unmatched/Invalid/Collision status. `valid()` refuses
empty, failed or conflicting previews. `vault::apply_rename` revalidates IDs,
parents, original names and the complete final sibling namespace. Sibling name
identity remains exact/case-sensitive, matching existing vault semantics;
case-distinct existing siblings remain distinct. Case-only changes are allowed
unless they collide with another exact sibling name.

Both functions require exclusive ownership of the tree. There is one linear
traversal plus ordered maps/sets (O(N log N)), not a path lookup per target.
Root rename is rejected. Descendants are traversed iteratively. All allocations
for replacement/rollback names precede mutation. Changes use no-throw swaps,
followed by one `commit_staged`; an RAII guard restores names on failure or
exception. Unchanged batches do not commit. Media IDs, references and encrypted
object files remain unchanged. A stale preview is rejected, not recomputed at
apply time. Grid selection is restored by ID after the listing refresh.

**Necessary integration change in `v3_db.cpp`:** the existing full metadata
transaction freed sibling-order values before sequential upserts but did not
free sibling *names*. Consequently a valid swap/cycle failed the database's
unique `(parent_id, display_name)` constraint. Within the same transaction,
`sync_metadata` now verifies stored names are TEXT and temporarily uses unique
BLOB node IDs as placeholders. SQLite compares those distinctly from all TEXT,
even text containing NUL. The existing writer replaces every live placeholder
with the final TEXT name, and deletes dead nodes before commit. Any failure rolls
back the placeholders. No schema change, intermediate durable rename, disk
object move, or `vault.cpp` edit is involved. The new cycle/reopen tests and
existing failed-sync database tests cover this path.

## Regex dependency and limits

PCRE2 **10.49**, commit `6f9d7c1373262c541324a16a358785b33ef116cf`, BSD-3-Clause,
is pinned as `vendor/pcre2`. Premake builds the 8-bit static interpreter using
upstream generic headers and distributed character tables via `pcre2-shim`.
No JIT, POSIX wrapper, system dependency or download during build. The normal
workspace sanitizer flags instrument PCRE2 too. Packaging includes its license.

Limits per compiled expression / per target substitution:

- Pattern and replacement: 4,096 bytes each; subject/result: 255 bytes.
- Compiled pattern: 1 MiB; parenthesis nesting: 64.
- Match limit: 100,000; interpreter depth: 256; match heap: 1,024 KiB.
- Aggregate PCRE2 allocation budget: 8 MiB, including compile-time allocations.
- Automatic callouts: 100,000 per substitution, with a 50 ms deadline checked at
  each callout and cancellation checked at the same points. PCRE2's native match,
  heap and depth limits remain active between callouts. This is bounded work,
  not a hard real-time scheduling guarantee.
- `\C` is forbidden so replacement cannot split a UTF-8 code unit sequence.

The 50 ms budget is per target, so a large selection of adversarial patterns can
still take time overall; the modal remains responsive and cancellable between
and during matches. A limit invalidates the preview; it never applies a subset.
See upstream [API documentation](https://www.pcre.org/current/doc/html/pcre2api.html),
[release notes](https://github.com/PCRE2Project/pcre2/releases), and
[security advisories](https://github.com/PCRE2Project/pcre2/security).

## Validation and integration handoff

Tests were added before the backend implementation (initial compile failed on
missing `rename_batch.h`). Focused coverage includes captures, named captures,
Unicode, extension defaults, first/all-match behavior, invalid captures/patterns,
catastrophic-backtracking limits, unsafe names, collisions, case-distinct siblings,
cancellation, stale previews, recursive selection, swaps, three-way cycles,
rollback/reopen, read-only rejection and byte-identical encrypted media objects.
The modal test covers worker preview/apply, editing invalidating a preview, and
close/join before vault teardown.

Validation completed here: **17/17 focused tests pass** (8 new rename/modal
scenarios plus 9 existing database regressions). The same 17 pass with ASAN and
UBSan instrumentation on PCRE2, the rename/modal code, and `v3_db.cpp`; unchanged
linked objects/dependencies remain the existing non-sanitized builds. Leak
checking passes outside the sandbox; the sandbox prevents LeakSanitizer's thread
inspection. Logs: `/tmp/osv-122c-focused.log` and
`/tmp/osv-122c-asan-unrestricted.log`. Changed C++ files pass `-Wall -Wextra
-Werror` syntax checks, changed lines are clang-formatted, and package shell
syntax / Git whitespace checks pass.

The isolated full build initially hit the shared disk quota. At the parent's
request it was stopped; only this worktree's copied dependency/build outputs
were removed. Existing dependencies/prefixes are read-only symlinks to the main
checkout. New PCRE2 and changed objects are built locally. The focused executable
links unchanged existing project objects as read-only inputs. This is not a
substitute for an integrated rebuild. The parent will run the full Debug,
Release, no-AV, ASAN, CI and Sonar gates after stacking.

Integration tasks for the parent:

- Initialize the new submodule (`git submodule update --init vendor/pcre2`).
- Review the small shared `GalleryGrid` and `v3_db.cpp` changes when stacking.
- Add the Shift+R controls and regex examples to README; update phase status and
  UI/vault/dependency/build memories. They were deliberately left untouched here.
- PCRE2's `LICENCE.md` is included in the raw Debug CI artifact and packaged
  releases; no additional license wiring is needed when stacking.
- Run actual SDL pointer/F1/dual-pane/lock tests, and record 1k/10k/50k event-loop
  responsiveness measurements. Headless lifecycle tests and worker ownership
  guards do not establish real-render timing or manual layout acceptance.
- Re-run `scripts/gen.sh` for the integrated compilation database. This worktree
  generated the equivalent Premake/Ninja database directly because symlinked
  submodules prevent the normal checkout-completeness check.

## Owned-worker follow-up

The bulk modal owns a `std::jthread` plus a `WorkState` containing an atomic
completion flag and an allocation-free failure enum, matching Phase B. There
are no futures, promises or catch-all handlers. Known allocation, length,
system, runtime and bounds errors are recorded by typed handlers; unexpected
exception types are not silently swallowed. Thread construction catches memory
and system failures and leaves the modal non-busy. Both polling and teardown
join before reading failure and releasing tree ownership. Failures produce a
fixed diagnostic, never exception `what()` text. The modal and name rollback
guard explicitly delete copy/move operations. No Sonar suppressions were added.

A regression test injects a throwing task through a private test peer, exercises
polling and teardown error paths, then verifies cancellation is observed and
the worker has finished before close returns. The focused runner uses existing
Release dependency objects as read-only inputs: the parent's evolving Phase F
Debug `ImportQueue` layout is incompatible with this older isolated base. An
integrated rebuild remains required; mixed-version Debug objects must not be
used to validate the isolated modal.

Follow-up validation: 20 focused checks pass (18 committed cases plus the local
benchmark and combined name/order transaction probe). The 18 committed cases
pass targeted ASAN/UBSan/leak checking with the updated worker code. The full
repository warning set (`-Wall -Wextra -Werror -Wshadow -Wconversion`) also
passes on the rename/modal files and tests; this caught and fixed an explicit
float conversion needed in full-name wrapping. Logs are
`/tmp/osv-122c-owned-all.log` and `/tmp/osv-122c-owned-asan.log`.
