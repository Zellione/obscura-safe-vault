# Argon2id cost benchmark (break-in effort Phase 5)

Standalone C tool that times **one Argon2id derivation** with the vault's
production parameters — `DEFAULT_KDF_PARAMS` (`src/crypto/kdf.h:28`):
**t=3, m=64 MiB, p=1** — using the exact Monocypher call the app makes
(`crypto_argon2` in `src/crypto/kdf.cpp:78`). The result is the honest
**per-guess cost** of an offline attacker who stole the vault file and must run
the KDF once per password candidate (the "key NOT leaked" scenario — the
complement of the Phase 1 core-dump break-in, where the key was leaked and the
KDF is irrelevant).

It is **not** part of the main `osv` build or CI; it exists to produce the
cold-attack estimate recorded in `docs/break-in-effort.md` (Phase 5).

## Build & run

```sh
make                 # builds ./kdf_bench against the vendored Monocypher
./kdf_bench          # 20 iterations (1 warm-up + 20 timed)
./kdf_bench 50       # or a custom iteration count (1..1000)
```

Requires only a C compiler and the vendored Monocypher
(`vendor/monocypher/src`), already in the repo. `make clean` removes the binary.

## Output

Per-iteration milliseconds, then min/mean/max, effective GB/s
(`t × m` of memory touched per pass) and **guesses/s**. Use the *mean* (or
min, to be conservative about clock throttling) for crack-time estimates:
`time-to-crack ≈ (password space size / 2) / guesses-per-second`.

Notes:

- Inputs are fixed representative bytes; Argon2id's cost does not depend on
  their value, only on the parameters.
- The app's domain-separated input encoding (`kdf.cpp:29-59`) adds a few bytes
  to the message — negligible against a 64 MiB × 3-pass memory core.
- Times are wall-clock on this machine; CPU frequency scaling, other load, and
  the memory subsystem all influence them. Record the host (CPU model, OS)
  alongside any number you cite.
