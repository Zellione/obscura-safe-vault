# Phase 122H — object preservation and recovery evidence

## Storage finding

The current v3 implementation already stores originals independently. Gallery
names and parent IDs are SQLCipher database rows; galleries do not contain packed
encrypted media files. `object_relative_path()` in `src/vault/v3_fs.cpp` places
immutable objects at `objects/<two hex digits>/<random object ID>.osvo`.
`v3_object_store.cpp` authenticates each object's vault/node/role identity;
originals and derived thumbnails/posters use separate objects. Video objects
contain independently authenticated frames for bounded random reads.
`vault.header` contains a wrapped master key; it is not an encrypted copy of the
entire metadata database.

The new fixture makes this concrete: five database nodes (root, two nested
galleries, two images) map to **three object files** (two originals and one
thumbnail). Both images deliberately have the same display name in different
parents, different tags and different contents. Neither gallery requires an
object file. Tests inventory ciphertext by relative path, then compare the
entire inventory byte-for-byte through upgrade and backup/restore.

Physically mirroring encrypted gallery directories would reveal grouping/depth,
require filesystem moves coordinated with database transactions, and add path
and directory-management costs. It would not remove the database mapping's
importance to recovery. Retaining the immutable object layout lets a schema
upgrade change metadata without rewriting original media. No format or
production-code change was necessary here.

## Existing coverage retained and run

| Exact test | Evidence already present |
|---|---|
| `v3_db_migrates_v1_transactionally_and_preserves_encrypted_content` | Successful v1 migration, duration/tag preservation, read-only inspection leaves old encrypted DB unchanged |
| `v3_db_invalid_snapshot_preserves_committed_tree` | Failed metadata synchronization retains committed tree/generation on reopen |
| `v3_db_tag_limit_is_per_node_and_failed_sync_rolls_back` | Failed tag snapshot rolls back |
| `v3_db_conversion_batch_rolls_back_nodes_objects_and_mapping_together` | Conversion references, nodes and mappings roll back together |
| `v3_mutation_fault_matrix_cold_reopens_to_prior_or_new_state` | Injected write/fsync/rename/directory-sync failures leave no missing references |
| `v3_mutation_fault_matrix_survives_subprocess_termination_and_cold_reopen` | Same publication faults followed by child `_exit`, then cold reconciliation |
| `v3_backup_is_deep_verified_complete_and_published_no_replace` | Complete/deep-verified backup and restore, destination collisions cannot overwrite |
| `v3_restore_rejects_incomplete_source_without_creating_destination` | Incomplete input cannot publish a restore destination |
| `v3_deep_verify_reports_unauthentic_referenced_object_without_deleting_it` | Authentication failure is reported without destructive recovery |

These tests do not themselves compare original ciphertext across schema
migration or assert nested backup/restore mappings. Those were the gaps, rather
than a need to duplicate the publication-failure matrix.

## New regression evidence

`tests/vault/test_v3_upgrade_preservation.cpp` adds two tests:

- **`v3_schema_upgrade_failure_retry_and_reopen_preserve_original_ciphertext`:**
  constructs a genuine encrypted v1 DB using the committed historical SQL
  fixture, with real authenticated objects. A deliberately colliding
  `nodes_new` table forces a SQL DDL failure at the second table rebuild, after
  the transaction has rebuilt `vault_meta`. A raw cold connection checks that
  both stored schema versions remain v1 and nodes/object references/tags remain
  present. After removing the injected obstruction, production migration and a
  cold root/database reopen succeed. Both version fields equal **`SCHEMA_VERSION`**,
  not a hardcoded current version. Header and every original/thumbnail path and
  ciphertext remain identical. All nested mappings and decrypted payloads are
  verified after migration and reopen.
- **`v3_encrypted_backup_restore_preserves_nested_mapping_and_immutable_v1_source`:**
  production read-only v1 inspection upgrades an in-memory copy. The complete
  encrypted snapshot and production restore retain the exact object inventory,
  header, parent/name mappings, per-node tags, tag description, generation,
  object IDs/roles and authenticated payloads. The original v1 encrypted DB
  remains byte-identical after backup; the backup's DB remains byte-identical
  after restore. Snapshot DB bytes have no SQLite plaintext header or fixture
  names, and the restored DB rejects a wrong key. Deep integrity verification
  succeeds on the restored mapping.

The v1 fixture version is intentionally frozen; the destination assertions use
the supported schema constant so Phase 122E's later migration can be exercised
by the same tests after integration. The failure trigger specifically covers
the existing v1 table-rebuild transaction, not every future migration step.
There are no production fault-injection hooks or schema changes in this commit.

## Validation and limits

Integrated stack: full Debug suite passes **2,529 tests**. Full integrated
ASAN/UBSan/LSan is running; the focused sanitizer evidence below also passes.

The focused runner includes the new file plus existing `test_v3_db.cpp`,
`test_v3_backup.cpp`, `test_v3_mutation.cpp`, `test_v3_recovery.cpp`, and
`test_v3_transaction_model.cpp`: **26 tests, zero failures**. It compiles the
local implementation and reads shared dependency archives without modifying or
copying them. Run log: `/tmp/osv-122h-tests.log`; runner:
`/tmp/osv-122g/build/focused-h/vault_tests`. The same **26 tests pass under
ASAN/UBSan with leak detection**, exit 0, using instrumented local storage
sources and the shared instrumented SQLCipher/OpenSSL libraries; log:
`/tmp/osv-122h-asan-tests.log`. No full build or dependency copy was made.
Full integrated build/CI/SonarCloud
remain parent gates. The premake test glob includes the new file automatically.

This establishes rollback/reopen consistency for an actual failed migration,
unchanged original object bytes, and a complete encrypted backup/restore
exercise. Existing subprocess tests cover termination after injected publication
failures; they do not claim arbitrary power-loss testing inside every SQLite
instruction. Recovery still requires the correct password/keyfile and an intact,
consistent encrypted metadata backup. Independent media files do not recover
lost names/hierarchy/tags if all database mappings are lost, nor protect against
shared device failure. The synthetic payloads test storage/authentication, not
image decoding or large-vault performance.
