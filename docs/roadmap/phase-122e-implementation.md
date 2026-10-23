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
conflict preservation, and the administration keyboard flow; the parent must
execute these in the integrated runner.

The isolated full build stopped on disk quota. Per parent instruction it was
not retried: copied build/dependency artifacts were removed only from this
worktree and replaced with read-only-use dependency symlinks. Focused binaries
were built outside the repository build tree. Do not run dependency build scripts
against these symlinks. Full Debug/Release/ASAN/no-AV gates, graphical interaction,
large-selection latency measurements, and SonarCloud remain parent integration
checks. Regenerate build files to include the new C++ translation units.
