# Junk-Tag Cleanup Pass — Design

**Date:** 2026-08-02
**Status:** Approved
**Follow-up to:** PR #148 (drop tags with no renderable text on import)

## Problem

PR #148 stops *new* junk tags at import time: tags with no renderable text
(no ASCII letter/digit — e.g. `[()]`, `[]`, `()`, CJK-only archive titles)
display as empty bracket shells because the UI font atlas bakes printable
ASCII only. Vaults imported before that fix still carry such tags. This
feature removes them on explicit user request.

## Behavior

On the tag-overview screen (Shift+T), **Ctrl+X** opens a Y/N confirm:
*"Remove all junk tags from this vault?"*. On confirm, every tag in the
vault failing `ui::tag_has_renderable_text` is removed from every node —
galleries, images, videos, and the root — in **one index commit**. A
transient summary reports *"Removed N junk tags from M items"* (mirroring
the Ctrl+I dict-import summary) and the overview reloads. If nothing
matches, no commit happens and the summary says so. N/Esc cancels.

## Architecture (mechanism/policy split)

The junk predicate lives in `ui` and `vault` must not depend on `ui`, so:

- **`vault::Vault::prune_tags(const std::function<bool(std::string_view)>& keep, PruneTagsStats*)`**
  — walks the index recursively from the root, erases every tag for which
  `keep` returns false, and calls `commit_index()` once, only if anything
  changed. `PruneTagsStats` returns `tags_removed` / `nodes_touched`.
  The predicate is a `std::function` (SonarCloud S5205; cold path, so the
  indirection is free). Returns `Locked` when locked, `InvalidArg` for an
  empty predicate.
- **`ui::TagOverviewScreen`** passes `ui::tag_has_renderable_text` as the
  policy; owns the `confirm_prune_` Y/N state and the summary display.

## Testing

Vault-level (TDD): prune removes matching tags across nested nodes and the
root, preserves non-matching tags and their order, persists across
lock/reopen, is a stat-zero no-op on a clean vault, and returns `Locked` on
a locked vault. The screen itself is SDL plumbing (per its own header) —
no new UI-model tests.

## Out of scope

No tag dedupe/normalization beyond the predicate; no automatic
unlock-time migration; junk tags arriving via cross-vault transfer
(`vault::combine`) are not filtered — the user can run Ctrl+X afterwards.
