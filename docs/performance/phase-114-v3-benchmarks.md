# Phase 114 v3 performance budgets

The opt-in Release benchmark uses the real vendored SQLCipher backend and real
directory/object filesystem primitives. Run each supported scale with:

```sh
OSV_V3_BENCH_SCALE=1000 build/bin/Release/osv_tests v3_scale_benchmark
OSV_V3_BENCH_SCALE=50000 build/bin/Release/osv_tests v3_scale_benchmark
OSV_V3_BENCH_SCALE=250000 build/bin/Release/osv_tests v3_scale_benchmark
```

Measurements below were taken on 2026-10-05 in the project Linux development
environment. They are regression baselines, not cross-machine product claims.

| Operation | 1k | 50k | 250k | Enforced budget |
|---|---:|---:|---:|---:|
| Batched metadata import | 98.5 ms | 5,318.9 ms | 12,819.8 ms | 1,000 ms + 0.20 ms/node |
| Root gallery listing | 0.7 ms | 38.4 ms | 285.4 ms | 250 ms + 0.08 ms/node |
| Name/tag search | 0.3 ms | 25.0 ms | 1,859.5 ms | 250 ms + 0.04 ms/node |
| Tag lookup | <0.1 ms | 0.1 ms | 0.1 ms | 100 ms |
| 100 encrypted 4 KiB range seeks | 13.7 ms | 13.3 ms | 12.3 ms | 1,000 ms |
| Quick verification | 1.2 ms | 43.4 ms | 225.5 ms | 500 ms + 0.02 ms/node |
| Reconcile and garbage collection | 0.1 ms | 0.1 ms | 0.1 ms | 500 ms + 0.02 ms/node |
| Encrypted SQLCipher backup | 3.9 ms | 91.7 ms | 455.1 ms | 1,000 ms + 0.08 ms/node |
| Delete half and resync | 51.9 ms | 2,845.6 ms | 13,905.5 ms | 1,000 ms + 0.20 ms/node |
| Database shutdown | 0.9 ms | 4.0 ms | 6.2 ms | 500 ms |

Unlock cost is intentionally dominated by the configured Argon2id KDF and is
covered by the KDF benchmark/tests rather than scaled by node count. Thumbnail
scroll and video seek share the bounded authenticated range-read path measured
above; codecs and rendering are independent of metadata cardinality. Full deep
verify and whole-vault backup are intentionally proportional to encrypted media
bytes, so this benchmark measures their metadata/object-discovery component.

The schema already carries the measured indexes (`nodes_by_parent_order`,
`objects_by_node`, and `node_tags_by_tag`). Bulk synchronization uses one SQL
transaction and reused node statements. The measurements did not justify a
second index, duplicate authoritative cache, WAL-mode change, or sharding.
