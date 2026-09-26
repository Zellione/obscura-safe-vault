# Directory-vault mutations and feature parity (Phase 112)

**Status:** not started

## Goal

Enable every normal vault mutation against v3 through the transaction coordinator and encrypted database/object store.

## Step-by-step work

1. Enable v3 creation with an exclusively created root, durable header, encrypted DB/schema/root row, verification reopen, and rollback limited to newly created artifacts.
2. Port image/video/folder/archive/PDF import staging: sanitize display names for database storage, encrypt originals and derived objects independently, then attach rows in bounded transactions.
3. Port gallery create/rename/move/copy/delete, sibling-collision policy, ordering, tags/favorites, tag descriptions/categories/templates/values, saved searches, and per-vault settings.
4. Port thumbnail/poster regeneration and migration watermarks using replacement-object ordering; old derived objects become GC candidates only after DB commit.
5. Port within-vault moves as database-only transactions; port cross-vault copy/move/combine by decrypting source into secure buffers and re-encrypting with fresh destination node/object identities.
6. Port selective export without weakening its consent and atomic no-follow plaintext sink. Never expose opaque object filenames as export names.
7. Port password/keyfile change by durably replacing `vault.header` through a two-copy or temp+rename protocol proven in the Phase 105 crash matrix; DB and objects remain untouched.
8. Integrate batch progress/cancel/error behavior and suppress auto-lock while exclusive jobs own the backend.
9. Keep legacy mutation paths operational through this phase to allow comparison and rollback; no new feature may update one backend only without an explicit parity decision.
10. Add behavioral contract tests that run the same mutation sequence against both backends and compare logical results.

## TDD and integration tests

- Full CRUD/batch/import/transfer/password-change sequences with reopen after every commit.
- Injected failures at every object and SQL boundary preserve old-or-new state and support retry.
- Cross-vault copies receive fresh identities; ciphertext/object swapping fails.
- Cancellation, auto-lock, shutdown, and worker races leave no live plaintext or dangling handles.
- Export remains the sole plaintext disk write and preserves warning/scope/wipe rules.

## Acceptance criterion

V3 supports all current user-visible mutations with logical parity, fresh cryptographic identities, crash-consistent publication, secure cancellation/lock behavior, and green Debug/Release/no-AV/ASAN/TSan gates.
