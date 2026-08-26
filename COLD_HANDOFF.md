# COLD HANDOFF — Vault break-in & hardening effort

**Date:** 2026-08-25 (session end)
**Repo:** `obscura-safe-vault` · **Branch:** `phase-87-migration-stale-pointers` · **PR:** #203
**Read this top-to-bottom, then start at §10 (Next steps).**

This is a cold-start handoff so a fresh session can continue the **break-in /
hardening effort** without re-deriving today's work. The general project
conventions live in `AGENTS.md`; this file is the *effort-specific* state.

---

## 1. TL;DR — where we are

- **Break-in is PROVEN** (Phase 1): an unstripped **core dump** left a real
  vault's **master key on disk**; we decrypted the index + an image (Poly1305
  **TAG VERIFIED**). The container crypto is fine — the leak is *key material in
  memory reaching disk*.
- **UAF crash root-caused + fixed** (Phase 3): a `MigrationJob` left the grid
  holding freed node pointers → SEGV. Shipped as **app Phase 87, PR #203** —
  CI green, **SonarQube `cpp:S6004` closed**, quality gate OK.
- **mlock/RAM exposure quantified** (Phase 4): the plaintext image is an
  **un-locked, swappable** buffer; today it lands in **zram (compressed RAM)**,
  **not** the physical NVMe (no disk swap/hibernate wired).
- **Effort plan documented** in `docs/break-in-effort.md`.
- **Remaining:** **Phase 5** (Argon2id benchmark → offline crack estimate) and
  **Phase 6** (hardening code PRs + system config).

**Attack vectors found so far:**
1. **On-disk memory images leak key material** (core dump proven; swap/hibernate
   are latent sinks). → Phase 1.
2. **Plaintext is swappable** — not page-locked, and the 8 MiB MEMLOCK budget
   can't hold a decoded image. → Phase 4.
3. **UAF (DoS + the crash that produced the core).** → Phase 3 / app Phase 87.

---

## 2. Goal & threat model

- **Goal:** get into a real `.osv` vault via *exploits* (not brute force),
  surface the genuine attack vectors, then harden against them.
- **Assumption:** the vault file's crypto (Argon2id + XChaCha20-Poly1305) is
  strong. The exercise targets the **running app + whatever the OS leaves on
  disk** while a vault is open.
- **Threat model:** a **local** attacker with read access to the user's home
  tree, core dumps, swap, and hibernation image — who can run the app and kill
  it. Not a remote attacker. (Extends AGENTS.md's accepted "framed vaults reveal
  plaintext *compressibility*" model to the key material itself.)

---

## 3. Scope (owner decisions — do not revisit without asking)

1. The **2nd vault** is **in scope** (it was open/unlocked at the crash).
2. **No** manual unlock of the **primary** vault (its key was not in the core).
3. **Delete** the stale core dump once evidence is extracted.
4. Hardening = **code PRs AND system config**.

---

## 4. Phase plan

| # | Phase | Status | Outcome |
|---|-------|--------|---------|
| 1 | Break-in via the on-disk core dump | ✅ DONE | Recovered the 2nd-vault master key; decrypted index + image, Poly1305 TAG VERIFIED |
| 2 | Live-repro on the primary vault | ⏭ SKIPPED (default) | Primary's key wasn't in the core; needs a manual unlock the owner declined |
| 3 | Root-cause + fix the UAF crash | ✅ DONE → **app Phase 87**, PR #203 | Post-migration refresh + exclusivity honoured; CI + SonarCloud green |
| 4 | Quantify mlock / RAM exposure | ✅ FINDINGS | Plaintext is an un-locked, swappable buffer; today **RAM-resident (zram), not on the NVMe** |
| 5 | Argon2id benchmark → cold-attack estimate | ✅ DONE → PR #205 | `tools/kdf_bench`: 100 ms/guess (≈10/s) on 8845HS; GPU est. 10^4–10^6/s; ≥8 chars infeasible |
| 6 | Remaining hardening (code + system config) | 🔄 IN PROGRESS | 6a ✅ (Phase 88, PR #206 carrier) · 6b pixels→SecureBytes · 6c budget UI · 6d system docs — **one PR at the end** |

Full narrative lives in `docs/break-in-effort.md`.

---

## 5. Phase 1 — Break-in via the core dump (DONE)

**Vector.** A SIGSEGV left an **unstripped core dump** on disk. Because the
crashing build was a **Debug** build (symbols present), the core still held the
unlocked `Vault`'s master key.

**Repro (evidence in `/tmp/osvbreakin/` — see durability note §9):**
1. `coredumpctl dump 223157 -o /tmp/osvbreakin/osv.core` → 621.8 MB core.
2. gdb read the **2nd-vault master key** straight out of the live `Vault`:
   - **Master key (hex):** `98109d96080827c31795f287076726ae602b39a00b5012eb1c7c51152ed890ab`
3. Decrypted the **active index slot** (`index_slot0.bin`, 34.3 KB) → Poly1305 **TAG VERIFIED**.
4. Decrypted the first **image chunk** → Poly1305 **TAG VERIFIED** → valid
   **3955×2225 JPEG** → `recovered_photo.jpg` (md5 `6b97bcf4ae64020f745bd9c1c822e78c`).

**Takeaway.** Any on-disk memory image captured while a vault is open leaks the
full master key → the *entire* vault is decryptable offline. The leak is **key
material in memory reaching disk**, which best-effort `mlock` does not guarantee.

---

## 6. Phase 3 — UAF crash, root-cause + fix (DONE → app Phase 87, PR #203)

**Crash stack** (from the core):
```
byte_at (src/gfx/text.cpp)
  ← next_codepoint
  ← FontAtlas::measure
  ← GalleryGrid::fit_name (src/ui/gallery_grid.cpp:2386)
  ← elide_middle (src/ui/widgets.h:110)
  ← render_grid_tile (src/ui/gallery_grid.cpp:2199)
  ← GalleryGrid::render
  ← App::render_frame (src/app/app.cpp)
```
Faulting node: `children_[0]`, its `name` `std::string` freed
(`_M_p==0x0, _M_length==17, _M_capacity==30`). At crash: migration job just
finished (`result_open==true, thumbs_fixed==205, reclaimed_bytes==3662185`),
`screen_` was the `GalleryGrid`, all 17 workers parked in `futex_wait` → a
stale-pointer UAF, not a live race.

**Root cause.** After a `MigrationJob` finishes, the active screen is never
refreshed (`screen_->on_vault_changed()`), while `Vault::compact()` rebuilds the
tree into a copy and publishes with `root_ = std::move(new_root)` — **destroying
the tree the grid was listing**; every cached `const IndexNode*` dangles the
instant `take_outcome()` returns.

**Fix (shipped).**
- `app::apply_migration_refresh(has_active_vault, screen)` (new
  `src/app/migration_refresh.h`) — `on_vault_changed()` + `mark_dirty()`, called
  by `App::update` right after `take_outcome()` (src/app/app.cpp).
- While a `MigrationJob` is active, `App` pauses the screen's `update()` and
  skips its `render()` (the `migration_job.h` exclusivity contract).

**Deliberately unchanged:** `compact()`'s copy-rebuild-publish (crash-safe by
design), the `.osv` format / `INDEX_VERSION`. Tests: 2131/0, ASan clean, TSan no
new races. → `docs/roadmap/phase-87-migration-stale-pointers.md`, **PR #203**.

---

## 7. Phase 4 — mlock / RAM exposure (FINDINGS)

**Measured (this host, 2026-08-25):**
- `RLIMIT_MEMLOCK` = **8192 KB (8 MiB), soft *and* hard.** App requests a
  **256 MiB** page-lock budget (`src/app/app.cpp:91`), but soft→hard only, so it
  can lock **≤ 8 MiB** and warns *"…large decoded images may not be page-locked."*
- Decoded image = **3-channel RGB**, `width×height×3` bytes
  (`src/image/image.h:26`, `src/image/decode.cpp:54`). 12 MP = **36 MB** ≈
  **4.5× the budget**; the Phase 1 photo = **26 MB**.
- **Decoded pixels are NOT `SecureBytes`** — a plain `std::vector<uint8_t>`
  (`image.h:24-26`), by explicit decision: *"transient (never written to disk),
  so mlock is unnecessary."* Uploaded to GPU via `SDL_UpdateTexture`
  (`src/gfx/texture_cache.cpp:68`) then freed.
- Swap = **zram0 only** (4 GiB, compressed, RAM-resident). No disk swap/hibernate
  wired to the NVMe (holds only `/boot` vfat + `/` ext4). `vm.swappiness`=60.

**Conclusion — the invariant holds by *configuration*, not by `mlock`:**
1. The plaintext image is an **un-locked heap buffer** → swappable.
2. Its only sink is **zram (compressed RAM)** → a **physical-disk-read** attack
   doesn't find it, but a **cold-boot / live-forensics / core-dump** attack does
   (same class Phase 1 proved).
3. No disk swap/hibernate wired today → it doesn't reach the NVMe *now*;
   **enabling hibernate or a host with disk swap + an 8 MiB budget would.**

**Honest threat model today: RAM-residue, not disk.**

**Follow-ups (→ Phase 6):** pixel buffer → `SecureBytes` (attempt mlock + warn
+ `MADV_DONTDUMP`); recommend a `LimitMEMLOCK` that fits a decoded image; if
disk-strength is required, encrypted swap/hibernate (LUKS) or a documented
"no hibernate / no disk swap" policy.

---

## 8. Phase 5 — Argon2id benchmark (PENDING)

**What:** measure the real cost of the KDF (Argon2id **t=3 / m=64 MiB / p=1**)
to produce an **offline password-crack estimate** for a vault whose key was *not*
leaked — the honest strength of the container on its own (the complement to
Phase 1, where the key *was* leaked).

**Distinguish the two scenarios:**
- **Key leaked** (Phase 1/4, cold-boot/core/swap): KDF irrelevant — the attacker
  has the key and just runs XChaCha20-Poly1305 (fast).
- **Key not leaked** (vault file stolen): the attacker must run **Argon2id per
  guess** → this is what Phase 5 quantifies.

**How (concrete):**
- Time one Argon2id (t=3, m=64 MiB, p=1) on a representative CPU and, for the
  "GPU cold attack" estimate, on a high-end GPU (e.g. `hashcat -m 4300`, the
  `argon2` CLI, or a small C program over Monocypher's `argon2id`).
- Record seconds/op and effective GB/s; derive guesses/sec.
- Estimate offline crack time for a representative password space
  (e.g. 12–20 char, mixed, with/without keyfile).
- Record the numbers + the tooling used in `docs/break-in-effort.md` (Phase 5).

---

## 9. Phase 6 — Remaining hardening (IN PROGRESS)

Scope **confirmed by the owner** (de-duplicated against AGENTS.md hardening
notes). **One PR at the end** (owner instruction 2026-08-26): commit every step
to one branch, open a single PR covering all of it.

- **6a — Vault file perms** — ✅ DONE (app Phase 88, commit on
  `phase-88-vault-file-perms`, PR #206 is the carrier): exclusive owner-only
  create + best-effort tighten on open; 2134/0, ASan clean, CI green.
- **6b — Decoded pixel buffer → `SecureBytes`** (Phase 4) — the plaintext image
  is a plain `std::vector` (`image.h:26`); make it mlock'd so the
  degrade-to-swappable path is explicit + `MADV_DONTDUMP`'d. *Largest single
  code change; needs a TDD pass (decode path + texture upload + a
  `SecureBytes` API for `std::vector` ergonomics).*
- **6c — mlock budget in-UI** — app warns at startup when budget < 256 MiB;
  surface the state (e.g. F1 help / status) so a degraded run is visible.
- **6d — System config docs** — `LimitMEMLOCK` budget, zram/hibernate policy,
  Debug-vs-Release core-dump exposure (Debug dumps on by design; confirm
  Release suppression is sufficient, advise against a Debug build holding a
  live vault).
- **Deferred (owner):** `SecureBytes` for the index tree; clipboard gate for
  copied paths/names.

---

## 10. Next steps (execute in order)

1. **Confirm CI** run `32892254716` (branch `phase-87-migration-stale-pointers`)
   is green — it is markdown-only on top of already-green code, so it should be.
   (`gh run view 32892254716`)
2. **Owner merges PR #203.** ⛔ The agent does **not** merge (AGENTS.md step 7).
3. **Post-merge:** `git fetch`, confirm `ROADMAP.md` + the phase-87 doc are on
   `origin/main`; update the relevant `.serena/memories/` entries if the phase
   added/removed modules or changed symbols/conventions (see AGENTS.md §Serena).
4. **Preserve the `/tmp` evidence before any reboot** (see §11 durability): copy
   the two repro tools (`/tmp/osvbreakin/breakin.c`, `extract_photo.c`) to a
   durable location, and/or note they're regenerable from the original core.
5. **Delete the stale core** (owner, needs `sudo`):
   ```
   sudo rm /var/lib/systemd/coredump/core.osv.1000.3cd35ffc1d64420abeedbf19abb233d2.223157.1787679933000000.zst
   ```
6. **Phase 5** — Argon2id benchmark → cold-attack estimate (§8).
7. **Phase 6** — hardening code PRs + system config (§9).
8. **Write the final report** with the break-in transcript (recovered key, TAG
   verifications, the UAF root cause, the mlock finding).

---

## 11. Durability warning (read before rebooting)

- **The repro tools are now in the repo** — `tools/breakin/` (`breakin.c`,
  `extract_photo.c`, `Makefile`, `README.md`). The break-in is **reproducible
  from the repo alone** (needs the `.osv` vault + the key hex, both in the docs).
  Verified: rebuilt + re-ran — index + image both **TAG VERIFIED**, photo md5
  `6b97bcf4ae64020f745bd9c1c822e78c`.
- **`/tmp` is tmpfs (7.7 GB, in RAM) → CLEARED ON REBOOT.** Everything in
  `/tmp/osvbreakin/` (core copy, tools, decrypted index/photo) is lost on reboot
  — but the tools are now in-repo, so only the *convenience* artifacts go.
- **The original core is durable** (ext4 root):
  `/var/lib/systemd/coredump/core.osv.1000.3cd35ffc1d64420abeedbf19abb233d2.223157.1787679933000000.zst`
  — everything is **regenerable** from it via `coredumpctl dump 223157` + the
  tools. So no critical fact is lost on reboot; the *convenience* artifacts are.
- **Key facts are already in-repo** (durable): recovered key hex, md5, sizes,
  repro steps — in `docs/break-in-effort.md` and this file.
- `/tmp/osvbreakin/` contents at session end: `osv.core` (621.8 MB), `breakin.c`/
  `breakin`, `extract_photo.c`/`extract_photo`, `index_slot0.bin`,
  `photo_decrypted.bin`, `recovered_photo.jpg`, `thumb_preview.png`.

---

## 12. Critical technical context (don't re-derive)

**Vault file format (`.osv`):**
- Header (little-endian) offsets: `FLAGS=12`, `T_COST=17`, `MK_NONCE=46`,
  `WRAPPED_MASTER_KEY=70`, `MK_TAG=102`, `SLOT_A_OFFSET=118`, `ACTIVE_SLOT=206`.
- `flags=0` → legacy, **password-only** (no keyfile), **no framing**.
- KDF = **Argon2id** (Monocypher), params **t=3 / m=64 MiB / p=1**.
- AEAD = **XChaCha20-Poly1305** (24-byte random nonce; fresh per chunk).
- Media chunk layout = `nonce[24] | ciphertext | tag[16]` (authenticate before
  decrypt: `crypto_aead_unlock`).
- Index blob = `u8(INDEX_VERSION=12)` + root + saved searches + settings.

**Architecture (for the UAF + mlock findings):**
- `IndexNode.children` = `std::vector<IndexNode>` (value semantics,
  `src/vault/index.h:164`).
- `Vault::compact()` (`src/vault/vault.cpp:1764`) copies the tree, then
  `root_ = std::move(new_root)` (line 1892) — **destroys the old tree**;
  `src/vault/vault.h:373`: "Invalidates all IndexNode pointers."
- `GalleryGrid` holds `std::vector<const IndexNode*> children_`
  (`src/ui/gallery_grid.h:276`), re-fetched via `refresh()`
  (`gallery_grid.cpp:295`) ← `on_vault_changed()` (`:274`).
- `src/ui/migration_job.h:5-10`: job owns the vault exclusively; the screen must
  not read until `take_outcome()` (sets `active_=false`, joins worker —
  `migration_job.cpp:543`).
- Secure memory: `crypto::SecureBuffer<N>` / `crypto::SecureBytes`
  (`src/crypto/secure_mem.h`) — mlock'd + `crypto_wipe`'d, **warn-once on mlock
  failure, degrade to swappable**; `src/platform/harden.{h,cpp}` —
  `grow_secure_mem_budget` (soft→hard MEMLOCK), `disable_core_dumps` (Release
  only).

---

## 13. Environment & commands

- **Build:** `scripts/build.sh` (app), `scripts/gen.sh` (premake; regenerate
  `compile_commands.json` after adding/removing source files).
- **Test:** `scripts/test.sh` (Debug), `--release`, `--asan`, `--tsan` (mutually
  exclusive). Baseline **2131 tests, 0 failed.**
- **CI:** `.github/workflows/ci.yml` — gcc/clang Debug+Release (Linux), MSVC
  Debug+Release (Windows), ASAN+UBSan, ThreadSanitizer (`ubuntu-latest`), No
  FFmpeg, clang-format, **SonarQube analysis / SonarCloud**.
- **SonarQube:** use the MCP tools (do **not** install/auth sonarqube-cli — the
  agent can't do the auth flow). Project `Zellione_obscura-safe-vault`, PR by
  number. Re-scan is the "SonarQube analysis / Linux" CI job.
- **Note:** `osv_tests` does **not** compile `src/app/app.cpp` (only
  `src/app/back_click.cpp`) — so `app.cpp` changes are runtime-only; build the
  `osv` app target to confirm they compile. The `app::` free functions
  (`apply_migration_refresh`, etc.) are what the tests cover.
- **TSan baseline:** 5 `radeonsi_drv_video.so` data races in the video-decode
  worker tests are a **known Phase 42 issue** (local Mesa VA-API driver; CI's
  runner has none). Byte-identical with/without our change — not a regression.

---

## 14. Current git / PR / evidence state

- **Branch:** `phase-87-migration-stale-pointers`
- **Commits (top→bottom):**
  - `755d611` docs: record the vault break-in & hardening effort plan (Phases 1-6)
  - `25db8a7` Phase 87 — SonarQube: fold `migration_active` into an init-statement (S6004)
  - `95bc3e3` Phase 87 — Migration leaves the grid holding freed node pointers (SEGV)
- **PR:** #203 — https://github.com/Zellione/obscura-safe-vault/pull/203
  (CI run `32892254716` in flight; prior run `32891071369` fully green, SonarQube
  `cpp:S6004` CLOSED, quality gate OK).
- **Files changed this effort:**
  - `src/app/migration_refresh.h` (new), `src/app/app.cpp` (fix + S6004)
  - `tests/app/test_migration_refresh.cpp` (new)
  - `docs/roadmap/phase-87-migration-stale-pointers.md` (new), `ROADMAP.md`
    (Phase 87 row)
  - `docs/break-in-effort.md` (new), `COLD_HANDOFF.md` (new)
  - `tools/breakin/` (new: `breakin.c`, `extract_photo.c`, `Makefile`, `README.md`,
    `.gitignore`) — the reproducible break-in tools
- **Original core to delete** (owner, sudo): see §10 step 5.

---

## 15. Gotchas / notes

- **SonarQube auth:** agents can't complete it — use the MCP tools / CI findings.
- **`osv_tests` vs `osv`:** app.cpp is app-target-only (see §13).
- **LSP signedness** in `src/gfx/text.cpp`, `src/ui/gallery_grid.cpp`,
  `src/ui/import_queue.cpp`, `src/ui/tile_thumb.cpp` — pre-existing, unrelated.
- **`/tmp` tmpfs** — cleared on reboot (see §11).
- **PR cadence (owner, 2026-08-26):** don't open a PR per step — commit each
  step to one branch and open a **single PR at the end**. Owner merges; the
  agent never merges (AGENTS.md step 7).
- **`INDEX_VERSION` / `.osv` format** was NOT changed this effort — do not bump
  it unless a Phase 6 change requires it.

---

## 16. Cross-references

- Effort plan + findings: `docs/break-in-effort.md`.
- **Reproducible break-in tools: `tools/breakin/`** (`breakin.c`, `extract_photo.c`,
  `Makefile`, `README.md`) — build + run to re-derive the index + photo.
- App Phase 87: `docs/roadmap/phase-87-migration-stale-pointers.md`, **PR #203**.
- Conventions / invariants / hardening: `AGENTS.md`.
- Serena memories: `.serena/memories/` (start at `mem:core`); update per AGENTS.md
  §Serena after the merge.
- Evidence: `/tmp/osvbreakin/` (tmpfs) + the durable core in
  `/var/lib/systemd/coredump/`.
