# Phase 119 — Split-view cross-vault copy and transfer lifetime safety

Status: implemented; awaiting owner merge after green gates and review.
Branch: `phase-119-split-vault-copy`.

## Plan

1. Reproduce transfer ownership violations with screen-level regressions. While a
   worker owns either vault, suspend browsing, thumbnail/hover work, and navigation;
   join before refreshing cached node pointers, including partial/error outcomes.
2. In F3, Ctrl+O opens the vault selection/password/keyfile flow for the right
   pane. Keep the active vault on the left and distinguish both vaults visibly.
   Use independent thumbnail caches and explicit lifetime ownership. Ctrl+W
   closes the second vault; leaving split view locks it as well.
3. Tab chooses the source pane. M copies selected media and gallery trees into
   the other pane's browsed gallery, with existing conflict handling. Cross-vault
   copying defaults to copy and never removes originals. Same-vault move/copy
   remains available. Legacy vaults may be sources but never destinations.
4. Exercise both directions, identical gallery paths, conflicts, cancellation,
   failed unlock, and teardown. Run Debug, Release, ASAN/UBSan, and no-AV gates;
   update docs and Serena memories in the phase PR, then verify CI/SonarCloud.

## Scope and invariants

The second pane is a session-local copy browser. Opening it does not replace the
active vault or persist its credentials. All copy data uses existing secure
memory and authenticated transfer APIs; no plaintext temporary files or format
changes. The owner merges after green checks and review.

## Crash regression

A deterministic worker/UI test reproduces AddressSanitizer heap-use-after-free
in `GalleryGrid::update` → `rebuild_detail` → `SecureString::view`: a transfer
reallocates the tree while the detail panel still holds the old node pointers.
The update guard now polls and joins before any detail/hover work. Split-view
transfers suppress both pane updates/renders and modal navigation until joined,
and refresh both panes on every outcome. Synchronous same-vault listing changes
also refresh the sibling before it can read stale nodes.

The secondary pane owns its vault, cache, and session; moving a warm handle into
it removes the handle from timeout management. Native folder/legacy-file browse
is available in the picker. Copying is bidirectional, including mixed selections
and identical source/destination gallery paths in different vaults. Full viewer
and other editing tools remain available by opening that vault from the manager.

## Validation

Screen regressions cover bidirectional v3 copy, mixed selections, same-name
conflict suffixing, failed authentication, early F3 refusal, Ctrl+W writer-lease
release, warm-slot ownership, cancellation/shutdown, and same-vault moves. The
normal pull-import regression exercises 40 incoming gallery trees. A paused
worker regression detects the original stale-node read under ASAN. Picker tests
also cover opening an unregistered directory and moving its unlocked ownership.

Local gates passed: Debug and Release each 2,461/0; ASAN/UBSan/LSan
2,461/0 outside the sandbox; no-AV Debug 2,265/0. Native SDL input/render
coverage and the paused-worker/same-vault regressions pass. Normal build files
are restored. `serena memories check` reports no referential integrity issues.
CI and SonarCloud are checked on [PR #256](https://github.com/Zellione/obscura-safe-vault/pull/256).
The owner merges after review and green gates.
