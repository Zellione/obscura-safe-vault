# Vault break-in & hardening effort

**Status:** ALL PHASES DONE. Phase 3 fix shipped (app **Phase 87**, PR #203); Phase 5 measured (`tools/kdf_bench/`); Phase 6 hardening shipped as app **Phases 88–90** (one PR).
**Opened:** 2026-08-25 · **Closed:** 2026-08-26

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
| 5 | Argon2id benchmark → cold-attack estimate | ✅ DONE | Measured 100 ms/guess (≈10/s) on Ryzen 7 8845HS; ≥8 random chars or 5 diceware words infeasible on a top GPU; unknown keyfile → effectively infinite space |
| 6 | Remaining hardening (code PRs + system config) | ✅ DONE → **app Phases 88–90** | 6a vault perms 0600 + no silent truncate · 6b pixels→SecureBytes · 6c budget state in F1 · 6d system-config + core-dump docs |

## Phase 1 — Break-in via the core dump (DONE)

**Vector.** A SIGSEGV left an **unstripped core dump** on disk
(`core.osv.1000.…223157.….zst`, pid 223157). Because the crashing build was a
**Debug** build, the core still held the unlocked `Vault`'s master key.

**Repro.** The extraction tools are in the repo — `tools/breakin/`
(`breakin.c` decrypts the index slot, `extract_photo.c` decrypts an image chunk;
`make && ./breakin <vault> <key>` then `./extract_photo <vault> <key> index.bin`).
Evidence artifacts were in `/tmp/osvbreakin/` (tmpfs — cleared on reboot):
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

## Phase 5 — Argon2id benchmark → cold-attack estimate (DONE)

Measure the real cost of the KDF (Argon2id **t=3 / m=64 MiB / p=1**,
`DEFAULT_KDF_PARAMS`, `src/crypto/kdf.h:28`) to produce an **offline
password-crack estimate** for a vault whose key was *not* leaked — i.e. the
honest strength of the container on its own, the complement to Phase 1 (where
the key *was* leaked and the KDF is irrelevant).

**Method.** `tools/kdf_bench/` (in-repo) times one `crypto_argon2`
derivation — the *exact* Monocypher call the app makes
(`src/crypto/kdf.cpp:78`) — with the production parameters. 20 timed
iterations (1 warm-up), `CLOCK_MONOTONIC`.

**Measured (this host, 2026-08-26):**
- Host: **AMD Ryzen 7 8845HS** (Strix Point, 16 threads), Linux 7.1.9-arch1-2,
  Monocypher (vendored) via `cc -O2`.
- **Mean 100.0 ms/guess** (min 90.3, max 122.2) → **≈ 10 guesses/s per core**,
  2.0 GB/s effective (192 MiB touched per guess: 3 passes × 64 MiB).
- Argon2id is **memory-bound on a single dependent index chain** — the 64 MiB
  working set misses L3 (16 MiB) and each block's address depends on the
  previous one, so one core sustains only a few GB/s of the ~90 GB/s dual-channel
  DDR5 peak. That is *by design*: it is the anti-GPU/ASIC tax.

**GPU (high-end, e.g. RTX 4090-class) — labelled estimate, not measured here
(no GPU runtime on this host).** The per-guess cost is fixed at 192 MiB of
touched memory regardless of hardware; a high-end GPU (~1 TB/s) running many
candidate streams in parallel (hashcat-style) plausibly sustains 10^1–10^2
GB/s effective on this kernel, i.e. **≈ 10^4–10^6 guesses/s** (≈ 3–5 orders
of magnitude above the measured CPU core). We design against a **conservative
10^5 guesses/s**.

**Offline crack time (median of the space = space/2):**

| Password space | size | CPU @10/s (measured) | GPU @10^5/s (est.) |
|---|---|---|---|
| random 6 chars (95^6) | 7.4×10^11 | 1,200 y | **43 days** |
| random 7 chars (95^7) | 7.0×10^13 | 1.1×10^5 y | **11 y** |
| random 8 chars (95^8) | 6.6×10^15 | 1.05×10^7 y | **1,050 y** |
| random 12 chars | 5.4×10^23 | 8.6×10^14 y | 8.6×10^10 y |
| diceware 4 words (7776^4) | 3.7×10^15 | 5.8×10^6 y | **580 y** |
| diceware 5 words | 2.8×10^19 | 4.5×10^10 y | **4.5×10^6 y** |
| top-10k wordlist ×4 | 1×10^16 | 1.6×10^7 y | 1,600 y |

(At the most optimistic 10^6 guesses/s: 6 chars → 4 days, 7 chars → 1 year,
8 chars → 105 y. The *threshold* is stable: **≈ 8 random chars or 5 diceware
words is the infeasibility line** on commodity hardware.)

**Conclusion — the container alone is honest and strong:**
1. A **≥ 8-char random** or **5-word diceware** password is infeasible to crack
   offline on a single high-end GPU (≥ 10^3 y), and utterly infeasible on a
   laptop CPU (≥ 10^7 y).
2. **≤ 7 random chars / 4-word phrases are *not* safe** against a determined
   offline attacker with a top GPU (days–years) — password *length*, not the
   algorithm, is the variable that matters here.
3. **A keyfile the attacker does not have makes the password space effectively
   infinite** (the keyfile bytes enter the KDF input: an N-byte unknown keyfile
   multiplies the search by 256^N — e.g. 64 bytes → ×10^192). This is why the
   "password + keyfile" two-factor wrap is the real protection, and why losing
   the keyfile (like losing the password) locks the vault forever.
4. This quantifies the *complement* of Phase 1: with the key **leaked** (core
   dump / cold boot / swap) the KDF cost is zero and the vault is trivially
   decryptable; with the key **not** leaked, these KDF parameters make
   password-only cracking infeasible at reasonable password lengths.

## Phase 6 — Remaining hardening (DONE → app Phases 88–90)

Scope **confirmed by the owner** and de-duplicated against the AGENTS.md
"Hardening notes". Shipped as three app phases (one PR):

- **6a — Vault file perms → app Phase 88.** New vaults are now created
  atomically and **owner-only** (`0600` / current-user DACL — the same
  guarantee a keyfile already got), and an existing file at the target path is
  **refused** (`AlreadyExists`) instead of being silently truncated by the old
  `"w+b"`. Pre-existing vaults are tightened to owner-only (best-effort,
  warn-once) on `Vault::open`, so they self-heal. *The more secret file no
  longer had looser permissions than the less secret one, and the data-loss
  clobber path is gone.*
- **6b — Decoded pixels → `SecureBytes` → app Phase 89.** Closes the Phase 4
  finding directly: `ImageData::pixels` is now mlock'd (best-effort) +
  `MADV_DONTDUMP`'d + `crypto_wipe`'d on destruction, and the degrade-to-
  swappable path is explicit (`is_locked()`) instead of a plain unmarked
  vector. `SecureBytes` gained `operator[]` / `assign(span)` / `fill(n,v)`.
- **6c — Secure-memory state in-UI → app Phase 90.** The F1 help now shows a
  live `Secure memory: <budget> page-lock budget …` line — active, or "… some
  decoded data is swappable (mlock exhausted)" — backed by
  `platform::lockable_budget_bytes()` + `crypto::mlock_failure_seen()`. Both
  wording paths verified in the running app (default budget, and `ulimit -l 0`).
- **6d — System config + core-dump docs → app Phase 90.** README now documents
  the 256 MiB startup grow, the F1 status line, the swap-vs-RAM / zram /
  hibernate / `hiberfil.sys` caveat (a host-policy decision, not app-enforceable),
  and — the exact Phase 1 vector — that Linux **Debug** builds keep core dumps
  by design, so a Debug crash's core is as sensitive as the vault
  (`coredumpctl list` / `sudo rm …`); prefer Release for a live vault.

**Deferred (owner):** ~~`SecureBytes` for the index tree~~ → **app Phase 91**
(delivered 2026-08-27: every index-tree string — node names, tags, categories,
descriptions, field values, saved-search names — lives in `crypto::SecureString`,
saved-search query clauses live in `crypto::SecureBlob`, all are mlock'd
best-effort and wiped on destroy, and unlock/save/CommitLane index-blob paths
use secure or wiping storage. Page-lock ownership is tracked per OS page so
freeing one allocator neighbor cannot unlock another). ~~Clipboard gate for
copied paths/names~~ → **app Phase 92** (delivered 2026-08-28: a machine-scoped
Allow / Warn / Disable gate over both clipboard write sites via
`platform::ClipboardPref` + `ui::clipboard_gate`; Warn parks plaintext in an
mlock'd wiped buffer behind the App's default-cancel confirm, Disable refuses
with no write, and the password auto-clear arms only when a sensitive confirm
actually writes — see `docs/roadmap/phase-92-clipboard-gate.md`). No deferred
items remain from the effort.

Test totals across the phase: 2168 / 0 failed; ASAN clean; TSan clean (only
the known local `radeonsi_drv_video.so` driver race, absent on CI runners).

## Evidence (preserved in `/tmp/osvbreakin/`)

- `osv.core` — the core dump (621.8 MB).
- `breakin.c` / `extract_photo.c` (+ built binaries) — the extraction tools.
- `index_slot0.bin` — decrypted index slot (TAG verified).
- `recovered_photo.jpg` — decrypted image (md5 above).
- **Original core to delete** (owner, needs `sudo`):
  `/var/lib/systemd/coredump/core.osv.1000.3cd35ffc1d64420abeedbf19abb233d2.223157.1787679933000000.zst`

## Cross-references

- **Reproducible break-in tools: `tools/breakin/`** (`breakin.c`, `extract_photo.c`) — build + run to re-derive the index + photo.
- **KDF benchmark: `tools/kdf_bench/`** — re-run `./kdf_bench` to re-measure the per-guess cost on any host.
- App ROADMAP Phase 87: `docs/roadmap/phase-87-migration-stale-pointers.md`, **PR #203**.
- Crypto / container: AGENTS.md *Security invariants*; `mem:vault_format`.
- mlock / hibernate / core-dump hardening: AGENTS.md *Hardening notes*.
- KDF / AEAD choices: AGENTS.md *Technology choices* (Monocypher, Argon2id, XChaCha20-Poly1305).
