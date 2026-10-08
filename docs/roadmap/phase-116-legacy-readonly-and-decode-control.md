# Phase 116 — Optional legacy conversion and one video decode control

Status: In progress.

## Goal

Owners can browse old single-file vaults without converting them. Video decoding
has one understandable hardware/software selection instead of two competing switches.

## Behavior

- After authenticating a legacy vault, offer **Open read-only** (Enter) and
  **Convert a copy** (C). Conversion remains optional and uses the existing verified
  copy workflow. Escape from its form returns to the choice; cancellation joins
  the worker before the source can be browsed. The original file remains unchanged.
- Read-only browsing keeps the existing mutation guards and skips automatic upgrades.
- F2 Playback has Auto-play videos and one **Video decoding** row:
  **Hardware (with fallback)** (default) or **Software**. Ctrl+Shift+H toggles
  the same selection; Ctrl+Shift+F is retired. Changes apply to the next clip.
- Existing hwaccel.conf files retain their effective behavior: hardware disabled
  or software forced maps to Software. Saving the selection clears the obsolete
  force-software override. Hardware failures still fall back automatically.
- F1, toast messages, README, and current-state memories explain the single control.

## Acceptance and validation

- Screen regression tests authenticate a real legacy file and enter read-only
  browsing without destination credentials; the source remains unchanged.
- Conversion remains reachable and backing out returns to the read-only choice.
- Settings navigation exposes one decode row and toggles both ways; legacy saved
  preferences preserve software selection.
- Debug, Release, no-AV, and sanitizer checks pass; PR CI and SonarCloud are clean.
- Owner merges the phase PR.
