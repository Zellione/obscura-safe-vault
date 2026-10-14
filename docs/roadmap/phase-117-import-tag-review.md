# Phase 117 — Review tags on import

Status: In progress.

## Goal

Review incoming tags from media datasets (including nested archive metadata),
plain-text tag lists, and JSON tag dictionaries before saving those tags.

## Plan

1. Test and implement a detached review model with reversible optional cleanup
   of angle-bracket and parenthesized annotations, selection, rename, and mapping
   to a tag already in the vault.
2. Add a shared modal showing source and destination tags. Include all/none,
   filtering, keyboard/mouse selection, rename, and a searchable existing-tag picker.
   Confirm applies the reviewed values; cancel discards the pending tag changes.
3. Route text lists and dictionaries through the modal before their commit.
   Preserve existing metadata when mapping to an existing dictionary tag;
   coalesce collisions case-insensitively in source order.
4. Collect dataset tag assignments in the background import queue. Once discovery
   finishes, review the complete batch on the main thread, including nested and
   multipart archives. Media import continues normally; cancelling the tag review
   keeps imported media but imports none of its pending tags. Pending reviews are
   session-only and discarded on lock/exit. No plaintext staging files.
5. Cover model behavior, dictionary application, queue collection, cancellation,
   and modal events. Run Debug/Release/no-AV checks; update docs and memories;
   open a PR and resolve CI/Sonar findings. The owner merges.

## Behavior

Cleanup defaults off. It removes `<...>` and `(...)`, including nested groups,
collapses resulting whitespace, and preserves square brackets. An unmatched
opener removes the remaining suffix; stray closers are removed. Preview is
reversible and never alters an explicit rename or replacement. Empty results
are skipped. Existing tags and media outside this import are never renamed.

## Acceptance

All four requested options are available in each supported import path; no
incoming tag is saved before confirmation. Mapping chooses an existing canonical
tag and preserves its descriptions, fields, and category styling. Tests and CI
pass, and SonarCloud has no unresolved findings introduced by the phase.
