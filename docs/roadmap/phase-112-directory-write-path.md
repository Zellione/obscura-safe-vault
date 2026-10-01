# Directory-vault mutations and feature parity (Phase 112)

**Status:** complete

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

## Delivered

- Added opt-in directory-vault creation (`Vault::create_directory`) with exclusive layout
  creation, durable header/database initialization, verification materialization, and
  descriptor-identity-checked rollback. Phase 114 remains the deliberate default-format
  cutover; legacy `Vault::create` is unchanged in this phase.
- Routed gallery/media CRUD, batch mutations, imports, tags/favorites, tag metadata,
  searches, settings, migration watermarks, and derived thumbnail/poster replacements
  through one SQL transaction. Immutable objects publish before their node/role reference;
  replacements become live in the same commit and abandoned staged objects remain safe GC
  candidates instead of poisoning later commits.
- V3 within-vault moves preserve node/object identities and update only metadata. Cross-vault
  copy/move/combine continues through locked secure buffers and destination staging, minting
  fresh destination node and object identities. Export keeps the existing explicit-consent,
  selection-only, atomic no-follow sink.
- Password/keyfile changes rewrap only the master key and durably replace `vault.header`
  through staging, file sync, rename, and directory sync; database and objects are untouched.
- Added backend contract, cold-reopen, unattached-object retry, creation rollback, header
  authentication, mutation, transfer, and password-change coverage. PDF import remains an
  explicit parity no-op because no PDF importer exists in either backend.
