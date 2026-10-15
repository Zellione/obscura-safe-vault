# Phase 118 — Rename tags in administration

Status: implemented; local gates passed, pending CI and owner review.
Branch: `phase-118-tag-renaming`.

## Behavior

In the tag overview (`Shift+T`), select a tag and press `R`. The rename modal
starts with its full name selected. Enter saves; Esc cancels. Editing shortcuts
stay inside the modal, and the opening R text event does not alter the name.
After success the list refreshes, clears its prefix filter, and selects the renamed tag.
The action is unavailable for read-only legacy vaults.

The rename matches the old tag case-insensitively throughout the vault, including
root/gallery/image/video assignments. It preserves the description and template
field values, and updates exact tag conditions in saved searches (include,
exclude, and groups). Search names, group names, and filename queries are unchanged.
Malformed saved-query blobs remain untouched.

Names are trimmed. Empty names, embedded NUL bytes, and names over 65,535 bytes
are rejected. Case-only renames are supported. A different existing target tag
is rejected, including tags present only in descriptions or field values; this
phase does not merge tags. Changing a category prefix is allowed only when the
new category has every field needed to preserve the tag's existing values.
Category definitions themselves are not renamed.

## Persistence and security

`vault::rename_tag` prepares secure replacement strings, then commits assignments,
settings, and saved searches together using the existing metadata transaction.
Failed commits restore the live strings and saved queries. Node identities remain
stable; no media objects are rewritten and no vault-format change is needed.
Production legacy vaults remain read-only.

## Validation

Regression tests cover nested gallery/image/video tags, root assignments,
case-insensitive matching, whitespace trimming, case-only renames, target
collisions (including metadata-only targets), invalid input, locked/read-only
vaults, saved-search conditions, persistence after reopening, incompatible
category fields, and rollback after an injected commit failure.

Local gates passed: Debug and Release each 2,433/0; ASAN/UBSan/LSan
2,433/0 outside the sandbox; no-AV Debug 2,237/0. Normal build files restored.
`serena memories check` found no referential integrity issues. CI and SonarCloud
are checked on the phase PR.
The owner merges after review and green gates.
