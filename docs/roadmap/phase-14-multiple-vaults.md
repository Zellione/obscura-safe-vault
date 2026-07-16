## Phase 14 — Multiple vaults ✅

**Goal:** Manage and open several vaults; move images between them.

### Tasks
- [x] **Recent-vaults registry** — `src/platform/vault_registry.{h,cpp}`: a config-dir list of known vault **paths only** (add/list/remove). It stores **no secrets** — no passwords, no keys, no keyfile contents.
- [x] `src/ui/vault_manager.{h,cpp}` — becomes the app's first screen: lists known vaults, plus create / open-other (file dialog) / remove-from-list. Selecting a vault transitions to the unlock screen for that path.
- [x] **Multiple vaults, single-active** — `App` owns one *active* unlocked `Vault` at a time (`unique_ptr`) driving the gallery, plus a `` ` `` quick-switch overlay + the manager to change vaults. **Deliberate deviation from the original "collection of unlocked vaults":** single-active / lock-on-switch (a second vault is unlocked only transiently during a transfer) keeps the in-memory key blast-radius to one — see the design spec §2.1. Idle auto-lock wipes the active key after 5 min.
- [x] **Move/copy between (and within) vaults** — `vault::transfer_image` / `transfer_gallery` with `TransferMode {Move, Copy}`: `read_image` into mlock'd `SecureBytes` → `add_image` into the destination → (Move only) `remove_image`/`remove_gallery` from the source; destination commits first (crash = recoverable duplicate). `&src == &dst` supported (same-vault), with a cycle guard for gallery moves. Plaintext exists only in the locked buffer (invariant #1).
- [x] Update `CLAUDE.md` (vault manager as first screen; new platform/ui modules) + `mem:core`.
- [x] `tests/` — registry add/list/remove + "no secrets persisted"; two vaults unlocked at once during a transfer; `transfer_image` checksum-matches in the destination and is gone from the source (verified across a reopen of both); both indices valid; `transfer_gallery` recursive round-trip; Copy keeps the source; same-vault move/copy + cycle rejection; `IdleTimer` unit tests.

### Acceptance criterion
✅ The manager lists multiple vaults; a vault can be unlocked, browsed, and
switched (manager or `` ` `` overlay); an image moved between vaults matches its
checksum in the destination and is gone from the source after reopen; copy
leaves the source intact; the registry never persists secrets; the active vault
auto-locks when idle.

### Delivered as 5 stacked PRs
1. Registry + manager + single-active App (#23)
2. Move images between vaults — `transfer_image` (#24)
3. Move whole galleries — `transfer_gallery` + `Vault::remove_gallery` (#25)
4. Copy mode + same-vault transfers — `TransferMode` (#26)
5. Idle auto-lock + `` ` `` quick-switch overlay
