# Vault leakage and corruption audit

Reviewed the production v3 object, metadata, staging, recovery, backup, and
password-wrap paths, plus shared chunk compression and legacy read/conversion
boundaries. This is a source review with targeted regressions, not a proof that
every possible storage or platform failure is covered.

## Confirmed findings and fixes

| Finding | Impact | Remediation and regression |
| --- | --- | --- |
| Snapshot traversal could fail validation while SQLite's last status was successful. | The partial live-node table could be committed, deleting previously saved nodes and object references. | Treat every failed traversal as failure and roll back. A zero-ID child test cold-reopens the database and checks the prior child and generation survive. |
| miniz convenience functions allocated plaintext dictionaries on the ordinary heap. | Compression/decompression state was neither page-locked nor wiped on free, despite secure outer buffers. | Use stream allocation callbacks backed by `SecureAllocator`. Tests observe wiped state on success and checksum failure, and exercise allocation failure. |
| Session garbage collection considered pending staged objects unreferenced garbage. | GC before metadata attachment could delete an import's original, after which a commit could reference a missing file. | Exclude staged object IDs under the session mutex. Test zero-grace GC before attachment, successful reads/commit, and collection of abandoned objects after reopening. |
| Published staging handles still accepted writes. | A retained handle could append to an immutable object after publication and invalidate its authenticated structure. | Reject writes once published. Test that a rejected append leaves the object length unchanged. |
| Publication retries skipped parent sync for an existing shard. | A shard left visible by a failed parent sync could be used for an acknowledged commit without establishing its directory-entry durability. | Sync the objects directory for existing shards too. Fault injection proves retry does not rename the object until that sync succeeds. |

All five regressions were observed failing before their fixes. No vault-format,
cipher, KDF, schema, or dependency changes are required. The extra directory
sync per object publication is an intentional durability cost.

## Validation

Required gates: full Debug and Release suites, ASAN/UBSan, no-AV build, changed-line
formatting, GitHub CI and the PR's SonarCloud findings. Final results are recorded
in the PR. Existing object authentication, tamper, subprocess crash, password,
backup/restore, transfer, and conversion tests remain part of these suites.

Best-effort locking still depends on the OS memory-lock budget. Existing
documented codec/driver limitations and the explicit plaintext export exception
remain applicable. Legacy mutation code is retained for fixtures; production
legacy vaults remain read-only.
