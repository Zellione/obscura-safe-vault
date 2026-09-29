# Independently encrypted object format and store (Phase 108)

**Status:** complete

## Progress

- ✅ Core immutable object writer/reader and descriptor-relative object opens.
- ✅ Exact preamble, authenticated header, adaptively compressed bounded frames, seek table and footer.
- ✅ Whole-object and bounded range reads into `SecureBytes`.
- ✅ Bounded pull-based producer streaming, cancellation cleanup, and structural inspection.
- ✅ Identity/key/role/owner substitution, bit-flip, frame-swap, length and publication-fault tests.
- ✅ Debug/Release/ASAN and no-AV matrices pass; hostile bounds and durable faults fail closed.

Final verification: 2,319 Debug/Release/ASAN tests and 2,125 no-AV tests pass.

## Goal

Store every original image, thumbnail, and poster in a separate immutable authenticated object file whose identity cannot be swapped across roles, nodes, vaults, or filenames.

## Object contract

Only a small fixed preamble needed for key selection and parsing is plaintext: magic, object-format version, `vault_id`, `object_id`, and bounded framing parameters. The encrypted/authenticated header holds role, owner `node_id`, media format, plaintext length, frame count/table data, and compression information. Associated data binds the full preamble plus domain, role, owner, and logical frame number. The final filename must encode the same `object_id`; mismatch is corruption.

Objects are immutable after publication. Updates always create a fresh random object ID and file. Nonces are fresh per record. Per-object keys are domain-derived from the master key, so copying ciphertext under another filename/node/role/vault fails authentication. Compression occurs before encryption and has explicit decoded-size limits.

Opening requires the database's expected role, owner, plaintext length, frame size/count, and
encrypted length. The canonical AEAD associated data binds values that are inside the encrypted
header, so those expected values are needed to attempt authentication; the reader then requires
the authenticated header, table, footer, preamble, filename, and physical length to agree.

## Step-by-step work

1. Specify byte offsets, endianness, version negotiation, record framing, maximum frame/table sizes, and truncation/overflow behavior in `docs/V3_VAULT_FORMAT.md`.
2. Implement object ID generation and strict filename/parser validation; reject zero/reserved IDs and collisions rather than regenerating invisibly in tests.
3. Implement streaming writer state: exclusive staging file, authenticated header, bounded plaintext frames, fresh nonce per frame, footer/table authentication, sync, close, publish, and directory sync.
4. Implement a reader that validates preamble/header/table before exposing bytes and authenticates each requested frame before returning plaintext.
5. Route all plaintext and compression buffers through `SecureBytes`/wiping allocators; keep ciphertext in ordinary byte buffers; preserve mlock degradation reporting.
6. Support originals and the distinct thumbnail/poster roles. Database rows record identity and sizes but never substitute for authenticated object metadata.
7. Add whole-object sequential streaming and bounded range reads needed by image decode and export; export remains the only consent-gated plaintext disk sink.
8. Add object inspection/verification APIs that reveal only IDs, sizes, and status to logs—not names or decrypted metadata.
9. Ensure failed/cancelled writes wipe plaintext, close handles, and leave at most an unpublished encrypted staging file.
10. Property-test parsing and arithmetic against arbitrary bytes and maliciously huge declared values.

## TDD and security tests

- Exact-byte header/AD/key-derivation vectors and round-trips for every object role.
- Bit flip, truncation, append, frame reorder, duplicate frame, ID/filename mismatch, owner/role/vault substitution, nonce/table corruption, and wrong-key rejection.
- Short writes and failures at every publication step never create a database-visible partial object.
- Large image data streams with bounded memory and secure-buffer wipe assertions.
- Canary plaintext absent from published/staging objects, including compression failure paths.

## Acceptance criterion

Image originals, thumbnails, and posters round-trip through independently encrypted immutable files; all substitution/tamper cases fail before plaintext use; failure paths meet secure-memory and durability rules; and the full test matrix is green.
