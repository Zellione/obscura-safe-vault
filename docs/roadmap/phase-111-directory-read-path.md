# Directory-vault read path and UI integration (Phase 111)

**Status:** not started

## Goal

Open and browse v3 directory vaults end to end without enabling general v3 mutation yet. Legacy vault reading remains unchanged.

## Step-by-step work

1. Introduce a storage-backend boundary used by app/UI/media code for unlock, tree queries, object reads, settings, search, and lock; do not spread v1/v3 branches through screens.
2. Teach file dialogs, recent-vault registry, normalization, and startup restore to accept both regular legacy `.osv` files and v3 `.osv` directories.
3. Implement v3 unlock: secure root open, lock policy, bounded header parse, KEK derivation, master-key unwrap, DB-key derivation, SQLCipher verification, schema/integrity checks, and safe failure teardown.
4. Materialize only the metadata needed by the current screen, or provide secure snapshots, so the database—not a second serialized tree—remains authoritative.
5. Route gallery listing, nested navigation, covers, favorites, basic/advanced search, tags, saved searches, viewer strips, image reads, video reads, and F1 diagnostics through the backend.
6. Keep v3 in an explicit experimental/read-only mode. Build test fixtures through internal test helpers, not through production import UI.
7. Ensure auto-lock stops readers/workers, finalizes statements, closes DB/object handles, clears caches/snapshots, and only then wipes derived/master keys.
8. Add clear UX for wrong password/keyfile, busy vault, corrupt header/database/object, unsupported future version, and directory permission failures.

## TDD and integration tests

- The same logical fixture rendered through legacy and v3 backends yields equal listings, searches, tag inheritance, sort order, viewer order, and media bytes.
- Recent-vault paths distinguish file versus directory and survive Unicode/rename/missing-path cases.
- Lock during thumbnail/video/search jobs proves quiesce-before-wipe and no use-after-close.
- Malicious database values remain bounded and cannot become filesystem paths or log content.

## Acceptance criterion

An internally generated v3 vault can be unlocked and fully browsed/viewed with feature parity, auto-lock is safe, legacy reading is unaffected, and production UI cannot yet mutate v3 vaults accidentally.
