# Refactoring step 1 Explicit vault fixtures

The first implementation step from [R14 in the refactoring audit](../audit/refactoring-simplification-2026-10.md#r14-make-test-fixtures-identify-their-storage-backend)
removes implicit backend selection from the shared archive/import test fixture.
Previously `ziptest::make_vault` silently created a writable legacy vault and
discarded creation failure. Tests now explicitly request a legacy writable or
directory fixture and require successful setup.

## Changes

- `tests/vault/fixtures.h` owns the shared test credentials and checked directory,
  legacy writable, generic reopen, and legacy read-only reopen helpers. It does
  not depend on UI or archive helpers.
- All 139 callers of the old creation helper explicitly select the legacy
  writable fixture and check success. Their existing backend coverage remains
  intact. Archive tests retain compatibility aliases for shared credentials and
  generic reopening.
- Collision and deterministic queued-cancellation scenarios run against both
  backends and compare the original bytes after closing every handle and
  reopening the vault. A collision must retain the original file rather than
  silently replacing it with the incoming file.
- Nested ZIP import and confirmed tag-review behavior run against both backends.
  Directory folder import separately verifies that dismissing review keeps media
  but does not persist unconfirmed tags.
- A rejected final v3 snapshot must latch a commit-lane failure and preserve the
  previous committed tree on cold reopen. This uses an invalid in-memory node ID
  to exercise validation failure; it does not simulate disk-full or failed fsync.
- Directory copy and move retain the expected source/destination bytes after
  reopening both vaults.
- Import queue, folder import, and the new contract tests share a checked wait
  helper with an elapsed-time deadline. A deterministic parked-worker test
  verifies that timeout is reported as failure.
- Queue fixtures declare their commit lane before the queue, so an early test
  return destroys the borrowing queue before the lane it uses during teardown.

Application behavior, storage formats, dependencies, and production KDF costs
are unchanged. Later R14 work can expand backend-specific coverage as each
refactor needs it; this step does not claim to duplicate the entire legacy suite
against v3. The other audit findings remain proposed work.

## Validation

TDD: the fixture contract tests were added first and the build failed because
the explicit fixture interface did not yet exist. After adding the interface,
both fixture tests passed. New import/transfer characterization tests cover
existing behavior; they do not claim a newly fixed application defect.

Local Debug and Release suites each passed all 2,478 tests. ASAN/UBSan/LSan
passed the same 2,478 tests outside the sandbox, including the final fixture
lifetime corrections. The first sandbox run passed every test but LeakSanitizer
could not finish under ptrace; the unrestricted rerun completed cleanly.

The no-AV build passed all 2,282 tests. Normal build files and the compilation
database are regenerated afterward. Changed-line formatting, documentation
links, and `serena memories check` pass. The PR tracks GitHub CI and SonarCloud
results; both must be green before handoff. The owner merges.
