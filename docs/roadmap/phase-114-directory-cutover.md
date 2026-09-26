# Cutover, performance, fuzzing, and security audit (Phase 114)

**Status:** not started

## Goal

Make directory vaults the default for newly created vaults only after feature parity, performance, parser hardening, and security/durability audits pass. Legacy vaults remain openable but become read-only pending the final converter.

## Step-by-step work

1. Run representative 1k/50k/250k-node benchmarks for unlock, gallery listing, search, tag queries, import, thumbnail scrolling, video seek, deletion, verify, GC, backup, and shutdown; set regression budgets.
2. Optimize with measured SQL indexes, prepared-statement reuse, read snapshots, sharding, batching, and bounded caches without introducing a second authoritative index.
3. Fuzz the v3 header, SQL-facing decoded values, object parser/seek table, directory enumerator, and operation-state recovery. Retain the legacy fuzz corpus.
4. Audit every plaintext allocation and disk write. Confirm only explicit export writes plaintext; DB/journals/backups and every content object are encrypted; staging never holds plaintext.
5. Audit key lifetime/order, SQLCipher opaque-memory limitations, logs, core-dump behavior, symlink/hard-link handling, permissions, descriptor-relative access, and path normalization.
6. Execute the full crash matrix on real subprocesses/filesystems, including power-loss approximations around `fsync`, database commit/checkpoint, header replacement, object publication, GC, and backup.
7. Confirm copied/swapped DBs and objects fail by `vault_id`/identity binding and record the accepted whole-directory rollback limitation.
8. Switch Create Vault to v3. Mark legacy vault sessions read-only with a clear Convert action; do not delete the legacy backend because the next phase needs it.
9. Update README, UI help, vendored-dependency docs, security model, packaging, ROADMAP, and Serena current-state memories. Regenerate `compile_commands.json` and audit memories for stale single-file assumptions.
10. Run all required gates, CI, and SonarCloud. Stop for owner merge; do not begin conversion work on an unmerged storage cutover.

## Acceptance criterion

New vaults default to v3; the measured performance budgets and full test/sanitizer/fuzz/crash suites pass; the security review has no unresolved high/medium issue; documentation reflects the directory model; legacy vaults still open read-only; and the owner has merged the cutover PR.
