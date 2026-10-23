# Phase 122E — Persistent meta-tag bundles

Implementation branch: `work-122e`, based on `7e5ccd9`. This change is for the
parent's cascading PR integration; it does not claim completion of full CI,
ASAN, interactive UI acceptance, or SonarCloud gates.

## User interaction

In the selection tag editor, F3 opens vault-local bundles. Up/Down selects a
bundle; Enter applies it to the selection; Delete removes its application.
F2 creates a definition, F4 edits it, and F6 previews deletion. Administration
uses separate name and member fields, switched with Tab. Members are separated
by semicolons; a backslash escapes a literal semicolon or backslash. F5 previews
the number of affected applications and effective tag additions/removals;
Enter confirms and Escape cancels. Editing the name retains the bundle ID.
Definitions contain ordinary tags only, never references to other bundles.

The ordinary tag editor shows the selected tag's manual assignment count and
supplying bundles. Delete removes only the explicit assignment. An active bundle
or inherited tag can therefore keep that tag visible. Bundle administration,
preview, apply, and removal use the existing exclusive tag-editor worker, keeping
the owning screen's progress display responsive and joining before teardown.

## Model and transaction boundary

`IndexNode::tags` remains the local effective union consumed by search, counts,
and display. `explicit_tags` records manual ownership, `tag_bundles` contains
stable random 128-bit bundle IDs, and `tag_provenance` distinguishes legacy/new
uninitialized nodes. Pre-existing tags initialize as explicit. Ancestor
inheritance is calculated separately and never inserted by bundle application.
Manual promotion of an already-effective bundle tag is a real mutation.

`VaultSettings::tag_bundles` owns names and member tags. Definitions and their
strings use secure metadata storage. Definition edits/deletion and selection
apply/remove each commit once, restoring live provenance, effective tags, and
settings on failure. Selection cancellation before commit restores changes.
Overlapping bundles contribute independently. Ordinary tagging retains B's
worker and commit mechanism, now mutating explicit assignments.

For Phase 122D, include `vault/tag_bundles.h` and call
`visit_bundle_tag_references(root, settings, visitor)` while preparing the
category rename replacement list. It visits explicit assignments and all bundle
member strings, including unused definitions. Existing node tags must still be
visited separately. Individual tag rename already invokes this hook. Applied
bundle references survive tag/category renames because their IDs do not change.

## Persistence and compatibility

Encrypted SQLCipher schema version 3 adds `tag_bundles`, `bundle_tags`,
`node_bundles`, and `node_tags.explicit_assignment`. V2 upgrades transactionally;
existing node assignments default to explicit. V1 first uses its existing
migration, then upgrades to v3. No content object is rewritten. Read-only old
backups migrate an in-memory encrypted database copy; the source remains intact.
Older clients reject `user_version=3` through their existing version gate.

Foreign keys protect node/bundle relationships. Reopen checks that stored
effective tags match explicit assignments plus active definitions and rejects
inconsistent metadata. Existing encrypted database backups include all new
relationships. Legacy single-file serialization is unchanged and cannot create
bundles; conversion imports its existing assignments as explicit.

Cross-vault transfer carries active definitions and provenance. Matching names
and member lists reuse a destination definition. Conflicting definitions get an
explicit ` (import N)` suffix and, if necessary, a new ID; no existing definition
is overwritten. Existing destination manual tags survive gallery merging.
Subtree transfer retains inheritance within the copied tree. Tags inherited
from outside the transferred root (or from a standalone media item's ancestors)
are materialized as explicit at the transfer boundary, preserving visibility
when the source ancestors are absent. Inactive, unused definitions are vault
administration data and are not copied by a selection transfer. Whole database
backups preserve them. Transfers with applied bundles into a legacy writer are
rejected rather than flattening provenance.

## Validation and parent integration

Tests were added before the model implementation. Focused model runner: 2 tests
passed. Focused SQLCipher repository runner: 11 tests passed, including v1/v2
migration, encrypted backup/reopen, future-version rejection, and failed-snapshot
rollback. The v2 migration test caught and fixed a live SELECT locking the table
being replaced. Changed translation units passed strict no-AV syntax checks.
Additional Vault/UI integration tests cover manual promotion, overlapping bundles,
editing/deletion, bundle rename/reopen, cancellation, inheritance, and transfer
conflict preservation, and the administration keyboard flow. These were later
executed in the isolated integration follow-up below.

The initial isolated full build stopped on disk quota. At that point, per parent
instruction it was not retried: copied build/dependency artifacts were removed only from this
worktree and replaced with read-only-use dependency symlinks. Focused binaries
were built outside the repository build tree. Do not run dependency build scripts
against these symlinks. Full Debug/Release/ASAN/no-AV gates, graphical interaction,
large-selection latency measurements, and SonarCloud remain parent integration
checks. Regenerate build files to include the new C++ translation units.


## Isolated integration follow-up

The isolated no-AV Debug `osv_tests_Debug_x64` target was built against read-only
shared vendor prefixes, with compiler scratch under the dedicated
`build/tmp-phase122e` directory on project disk. No shared build targets or
vendor artifacts were modified. The linked build found and fixed the missing
`tag_editor_bundles.cpp` entry in the test target's explicit Premake source list.

Executed against the final follow-up build:

- `osv_tests tag`: **301 passed**, including all nine Vault bundle integration
  tests and all three TagEditor worker/administration tests.
- `osv_tests transfer`: **89 passed**.
- `osv_tests combine`: **29 passed**.

These filter sets overlap and are not a count of distinct tests. Full integrated
Debug/Release/ASAN and CI/Sonar remain parent gates.

Self-review added regressions for `set_tags`, manual promotion via batch add,
batch removal while bundles remain effective, duplicate selection paths,
overlapping bundles, definition edits/deletion, cold reopen, failed metadata
commits, real subtree/media transfer, and Combine conflicts. A deliberately
invalid unrelated node makes the metadata snapshot fail after the live tag edit,
checking restoration of explicit assignments, active bundle IDs, definitions,
and effective tags as well as the prior durable state.

The review reproduced and fixed three transfer problems: unsupported bundle
moves could prune the empty source gallery; a failed destination commit could
also prune source metadata; and Combine flattened bundle contributions into
manual tags, including when Combine copied a whole nested subtree. Gallery
metadata now uses `merge_tag_provenance`, preserving both
sources' manual ownership and independently applied bundles, with an atomic
commit for Combine or preparation within the subtree transfer's existing batch.
Failed metadata preparation marks its source branch as failed. Failed destination
commits stop source pruning. Legacy writer transfers retain their historical
flattened-tag behavior; v3 subtree copies preserve internal bundle inheritance.
Repeated application/removal with no changed nodes avoids an unnecessary commit.

The Phase 122D `visit_bundle_tag_references` integration hook is unchanged.
