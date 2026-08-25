# Vault break-in & hardening effort

**Status:** Phase 1, 3, 4 done (findings recorded); Phase 3 fix shipped (app **Phase 87**, PR #203); Phases 5–6 pending.
**Opened:** 2026-08-25

This is a deliberate **break-in exercise**: get into a real `.osv` vault through
*exploits* (not brute force), surface the genuine attack vectors, then harden
against them. The vault file's crypto (Argon2id + XChaCha20-Poly1305) is assumed
strong; the exercise targets the **running app + whatever the OS leaves on disk**
while a vault is open.

## Scope (owner decisions)

1. The **2nd vault** is **in scope** — it was open/unlocked at the time of the crash.
2. **No** manual unlock of the **primary** vault (its key was not present in the recovered core).
3. **Delete** the stale core dump once the evidence has been extracted.
4. Hardening = **code PRs AND system config**.

## Threat model

A **local** attacker with read access to the user's home tree, core dumps, swap,
and hibernation image — who can run the app and kill it. Not a remote attacker.
(Consistent with the accepted "framed vaults reveal plaintext *compressibility*"
threat model in AGENTS.md — this extends it to the key material itself.)

## Phase plan

| # | Phase | Status | Outcome |
|---|-------|--------|---------|
| 1 | Break-in via the on-disk core dump | ✅ DONE | Recovered the 2nd-vault master key; decrypted an index slot + an image, Poly1305 **TAG VERIFIED** |
| 2 | Live-repro on the primary vault | ⏭ SKIPPED (default) | Primary's key was not in the core; would need a manual unlock the owner declined |
| 3 | Root-cause + fix the UAF crash | ✅ DONE → **app Phase 87**, PR #203 | Post-migration refresh + exclusivity honoured; CI + SonarCloud green |
| 4 | Quantify mlock / RAM exposure | ✅ FINDINGS | Plaintext is an un-locked, swappable buffer; today it is **RAM-resident (zram), not on the NVMe** |
| 5 | Argon2id benchmark → cold-attack estimate | ⬜ PENDING | Offline password-crack cost of the container *alone* |
| 6 | Remaining hardening (code PRs + system config) | ⬜ PENDING | To be scoped / de-duplicated against AGENTS.md hardening notes |

## Phase 1 — Break-in via the core dump (DONE)

**Vector.** A SIGSEGV left an **unstripped core dump** on disk
(`core.osv.1000.…223157.….zst`, pid 223157). Because the crashing build was a
**Debug** build, the core still held the unlocked `Vault`'s master key.

**Repro (evidence in `/tmp/osvbreakin/`).**
- `coredumpctl dump 223157 -o /tmp/osvbreakin/osv.core` → 621.8 MB core.
- gdb (symbols present) read the **2nd-vault master key** straight out of the
  live `Vault` object:
  - **Master key (hex):** `98109d96080827c31795f287076726ae602b39a00b5012eb1c7c51152ed890ab`
- Decrypted the **active index slot** (`index_slot0.bin`, 34.3 KB) → Poly1305
  **TAG VERIFIED**.
- Decrypted the first **image chunk** → Poly1305 **TAG VERIFIED** → valid
  **3955×2225 JPEG** → `recovered_photo.jpg` (md5 `6b97bcf4ae64020f745bd9c1c822e78c`).

**Takeaway.** Any on-disk memory image captured while a vault is open — core
dump, hibernation file, swap, crash report — leaks the **full master key**, which
makes the *entire* vault decryptable offline. The container is strong; the leak
is **key material in memory reaching disk**, which the "no plaintext to disk /
mlock" invariant is meant to prevent but **best-effort `mlock` does not guarantee**.

## Phase 2 — Live-repro on the primary vault (SKIPPED)

Would re-run the same extraction against the primary vault to confirm the class
of leak holds there too. Skipped by default: the primary's key was not present in
the recovered core, and the owner declined a manual unlock of that vault.

## Phase 3 — UAF crash, root-cause + fix (DONE → app Phase 87, PR #203)

The SIGSEGV that *produced* the core was the app's own use-after-free: after a
`MigrationJob` finished, the active screen was never refreshed
(`screen_->on_vault_changed()`), so `GalleryGrid` kept rendering `children_`
pointers into a tree `Vault::compact()` had since freed → torn `name` string →
SEGV in `FontAtlas::measure`. Fixed by refreshing the active screen after
`take_outcome()` and pausing the screen's `update()`/`render()` while the job
owns the vault exclusively.

→ **app ROADMAP Phase 87**, `docs/roadmap/phase-87-migration-stale-pointers.md`,
**PR #203** (CI green, SonarCloud clean).

## Phase 4 — Quantify mlock / RAM exposure (findings)

**Question:** does the "no plaintext to disk" invariant actually hold on a real
host, given best-effort `mlock`?

**Measured (this host, 2026-08-25):**
- `RLIMIT_MEMLOCK` = **8192 KB (8 MiB), soft *and* hard.** The app requests a
  **256 MiB** page-lock budget at startup (`app.cpp:91`,
  `grow_secure_mem_budget`), but on this host it can only honour soft→hard, so
  the process can page-lock **≤ 8 MiB** total and logs *"secure-memory budget
  below 256 MiB — large decoded images may not be page-locked."*
- Decoded image buffer = **3-channel RGB**, `width×height×3` bytes
  (`image.h:26`, `decode.cpp:54`). A 12 MP photo (4000×3000) = **36 MB** ≈
  **4.5× the budget**; the Phase 1 photo (3955×2225) = **26 MB**.
- **The decoded pixels are not `SecureBytes`** — they are a plain
  `std::vector<uint8_t>` (`image.h:24-26`), by explicit decision: *"decoded
  pixels are transient (never written to disk), so mlock is unnecessary."* They
  are uploaded to the GPU via `SDL_UpdateTexture` then freed
  (`texture_cache.cpp:68`, `full_tex_cache.cpp:21`).
- Swap = **zram0 only** (4 GiB, compressed, RAM-resident). No disk-backed swap
  partition or swapfile; the NVMe holds only `/boot` (vfat) + `/` (ext4).
  Suspend-to-disk (true hibernate) is **not currently wired** to the NVMe,
  though the kernel exposes the `disk` state. `vm.swappiness` = 60.

**Conclusion — the invariant holds today by *configuration*, not by `mlock`:**
1. The plaintext image is an **un-locked heap buffer** — never page-locked — so
   it is swappable.
2. Its only swap target is **zram (compressed RAM)** — so a **physical-disk-read
   attack does not find it**, but a **cold-boot / live-forensics / core-dump
   attack does** (it is in RAM, compressed). This is the same class Phase 1
   proved via the core dump.
3. There is **no disk swap/hibernate wired today**, so the plaintext does not
   reach the NVMe *now*. **Enabling true hibernate, or running on a host with a
   disk swap + an 8 MiB MEMLOCK budget, would write it to the physical disk.**

So the honest threat model today is **RAM-residue** (cold-boot / live-dump /
core), not **disk**. `mlock` is keeping the data out of *disk* swap only by the
luck of the current zram-only config — it is not locking the pixel buffer at all.

**Follow-ups (→ Phase 6):**
- Make the decoded pixel buffer a `SecureBytes` (attempt mlock + warn-once +
  `MADV_DONTDUMP`) instead of a plain vector — closes the "plaintext is a plain
  heap buffer" gap and makes the degrade-to-swappable path consistent.
- Recommend a `LimitMEMLOCK` large enough for a decoded image (or document the
  8 MiB floor as a hardening precondition), so the page-lock is actually
  achievable.
- If disk-strength is required: encrypted swap/hibernate (LUKS-backed) or a
  documented "no hibernate / no disk swap" policy.

## Phase 5 — Argon2id benchmark → cold-attack estimate (PENDING)

Measure the real cost of the KDF (Argon2id **t=3 / m=64 MiB / p=1**) to produce
an **offline password-crack estimate** for a vault whose key was *not* leaked —
i.e. the honest strength of the container on its own, the complement to Phase 1
(where the key *was* leaked).

## Phase 6 — Remaining hardening (PENDING)

Candidate items to be **scoped and de-duplicated against the existing AGENTS.md
"Hardening notes"** before opening PRs (some may already be covered):

- **Vault file perms** — ensure new/existing `.osv` files are `0600`.
- **Core-dump exposure** — the crashing build was Debug (dumps on by design);
  confirm the shipped (Release) suppression is sufficient and whether a Debug
  build should ever be the one holding a live real vault.
- **Decoded pixel buffer → `SecureBytes`** (Phase 4) — the plaintext image is a
  plain `std::vector` (`image.h:26`); make it mlock'd so the degrade-to-swappable
  path is explicit + `MADV_DONTDUMP`'d, consistent with the rest of the code.
- **`SecureBytes` for index buffers** — keep the index tree in mlock'd memory.
- **Clipboard gate** — keep copied paths/names from leaking vault-internal names.
- **mlock budget check** — the app already warns when the budget < 256 MiB
  (`app.cpp:93`); decide whether to also surface it as a first-class status
  (e.g. in the `F1` help) so the "may be swappable" state is visible in-UI.
- **System config** — `RLIMIT_MEMLOCK` budget, zram/hibernate policy.

## Evidence (preserved in `/tmp/osvbreakin/`)

- `osv.core` — the core dump (621.8 MB).
- `breakin.c` / `extract_photo.c` (+ built binaries) — the extraction tools.
- `index_slot0.bin` — decrypted index slot (TAG verified).
- `recovered_photo.jpg` — decrypted image (md5 above).
- **Original core to delete** (owner, needs `sudo`):
  `/var/lib/systemd/coredump/core.osv.1000.3cd35ffc1d64420abeedbf19abb233d2.223157.1787679933000000.zst`

## Cross-references

- App ROADMAP Phase 87: `docs/roadmap/phase-87-migration-stale-pointers.md`, **PR #203**.
- Crypto / container: AGENTS.md *Security invariants*; `mem:vault_format`.
- mlock / hibernate / core-dump hardening: AGENTS.md *Hardening notes*.
- KDF / AEAD choices: AGENTS.md *Technology choices* (Monocypher, Argon2id, XChaCha20-Poly1305).
