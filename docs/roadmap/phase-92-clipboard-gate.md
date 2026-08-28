# Clipboard gate for copied paths/names (Phase 92)

**Status:** 🔜 ready for review
**Date:** 2026-08-28

## Problem

The break-in/hardening effort closed plaintext leaks *inside* the process —
Phases 88–91 made the vault file owner-only, the decoded pixels `SecureBytes`,
and the index-tree strings `crypto::SecureString`. But the **OS clipboard** is a
plaintext sink outside the app's mlock/wipe control: any process can read it,
and it persists past the app. The app wrote to it in two places:

- **Password / passphrase copy** — `UnlockScreen::copy_password_to_clipboard()`
  (Copy button + auto-copy-on-generate), Phase 45, behind an explicit action +
  ~25 s auto-clear.
- **Ordinary text-field copy/cut** — `ui::copy_selection_to_clipboard` /
  `cut_selection_to_clipboard`, Phase 54, which can copy node names, tags, and
  search text.

Both put plaintext (the very metadata Phases 89–91 just locked and wiped
in-memory) onto a readable-by-anyone, persistent channel — with no control over
whether the user wants that, and no way to ask first.

## What shipped

A machine-scoped **Allow / Warn / Disable** gate over every clipboard write,
mirroring the Phase 66/85 pref + F2-row pattern exactly:

- **`platform::ClipboardMode { Allow, Warn, Disable }`** +
  **`platform::ClipboardPref`** (new `src/platform/clipboard_pref.{h,cpp}`) —
  config-dir `clipboard.conf`, slugs `allow`/`warn`/`off`, atomic temp+rename,
  missing/unknown → **Allow** (a corrupt file can never lock out a legitimate
  copy or break startup).
- **`ui::clipboard_gate`** (new `src/ui/clipboard_gate.{h,cpp}`) — a
  UI-thread-only process global (like `media::autoplay_setting`): the mode, a
  pure `clipboard_gate_action(mode) -> {Copy, Confirm, Refuse}` mapping, and the
  **Warn pending-confirm channel**. A Warn-gated attempt parks its payload in an
  `mlock`'d, wipe-on-destroy `crypto::SecureBytes` (it may be a password) plus a
  `sensitive` tag; the App renders a default-cancel confirm; confirm writes via
  the existing `ClipboardBackend` seam and retains the payload once for the
  unlock screen's auto-clear arming (`take_confirmed_copy()`); cancel wipes.
- **Both write paths route through the gate:**
  - `copy_selection_to_clipboard` / `cut_selection_to_clipboard` (shared
    `gate_selection` helper so copy vs cut cannot drift): **Allow** writes
    immediately; **Disable** wipes the selection and is a no-op; **Warn** parks
    it and writes nothing yet — and **cut deliberately does NOT delete the
    selection until its write actually lands**, so a declined-cancel cut cannot
    destroy text it never got to copy.
  - `UnlockScreen::copy_password_to_clipboard`: **Allow** is today's behaviour
    (write + arm the 25 s auto-clear); **Disable** wipes and stops; **Warn**
    parks `sensitive=true` and arms the auto-clear **only** when the confirm
    actually writes (`update()` polls `take_confirmed_copy()`), so a declined
    copy never arms a clear.
- **F2 Settings → Security** gains a second row, **Clipboard** — Allow / Warn /
  Disable, machine-scoped, live-saved to `clipboard.conf` in
  `apply_value_delta` (row-aware: row 0 still saves `second_vault.conf`). Seeded
  from the persisted pref at `App::init` and at `open_settings_overlay`; the
  runtime gate is re-synced on every handled settings event.
- **App-owned confirm modal** (default-cancel, rendered/evented in
  `App::OverlayDispatch` between help and settings so it can surface even from
  the settings prompt field): `Enter`/`Y` confirms, `Esc`/`N` cancels. The
  pending payload is **never drawn** — it may be a password. `App::shutdown()`
  and an idle auto-lock both `cancel_clipboard_copy()` so an unconfirmed
  password cannot linger or be stranded without its auto-clear.

## Tests

- `tests/platform/test_clipboard_pref.cpp` (4) — round-trip each mode,
  missing/garbage → Allow, empty pref saves nothing.
- `tests/ui/test_clipboard_gate.cpp` (12) — `clipboard_gate_action` mapping,
  mode get/set, request parks a pending confirm (sensitive/non-), empty request
  parks nothing, confirm writes via the backend seam and clears pending, confirm
  retains the copy for auto-clear arming (consumed on read), confirm without a
  pending is a no-op, cancel drops without writing, a new request replaces an
  unconfirmed one, reset clears everything.
- `tests/ui/test_clipboard.cpp` (+6) — Disable refuses copy/cut without touching
  the backend or the selection; Warn parks (returns false, nothing written, text
  intact) and writes only after `confirm_clipboard_copy()`; Allow still writes
  immediately.
- `tests/ui/test_settings_model.cpp` — Security section is now 2 rows; row 1
  cycles Allow→Warn→Disable→Allow both directions without disturbing row 0.

2191 tests / 0 failed (baseline 2168 + 23); ASAN clean; TSan clean (only the
known local `radeonsi_drv_video.so` driver race); Release + no-FFmpeg (2013/0)
legs green; clang parity build green.

## Deliberately unchanged

- **No `.osv` change, `INDEX_VERSION` stays 12** — machine-scoped config-dir
  pref + process global only; the container is untouched.
- **No auto-clear for text-field copies.** The scope decision was to gate
  *existing* copy paths, not to extend the Phase 45 time-bombed clear to every
  copy.
- **The 25 s password auto-clear and its "only clear if it still matches" rule**
  — it is retained and merely *armed by the gate's confirmed write* instead of
  by the pre-gate action.
- **`ClipboardPref` is not a vault setting** — like theme/autoplay/second-vault,
  it is per-machine: a user's clipboard privacy preference should follow the
  machine, not a shareable `.osv`.
- **Allow remains the shipped default**, so the gate changes nothing for users
  who do not visit F2 → Security.