# obscura-safe-vault — Core (memory graph root)

Multi-platform (Linux → Windows; no macOS) encrypted photo gallery. A single `.osv` vault
file. Decrypted data lives only in `mlock`'d heap — never written to disk, except the one
gated deviation (`ui::export.*`). Galleries freely nest and may hold any mix of images,
videos, and sub-galleries as direct children (no leaf-only restriction — sub-galleries display
first, then media).

This is the graph root: it holds the high-level map + cross-cutting invariants, and points to
the per-domain memories below. Read the module memory for whichever `src/` directory you're
working in.

## Source map (directory → module memory)

```
src/
  app/       state machine + SDL event loop, single-active vault, idle auto-lock, session state
  platform/  config dirs, file dialogs, vault registry, theme/volume prefs, hardening, error log
  crypto/    Monocypher wrappers (XChaCha20-Poly1305, Argon2id, CSPRNG, SecureBytes/mlock)
  vault/     .osv container: index, chunk store, transfer/combine, search, safe node names
  image/     stb/WebP/HEIF decode, thumbnails, off-thread decode worker
  media/     FFmpeg video/audio decode over encrypted chunks, hw accel, A/V sync (OSV_VENDORED_AV)
  gfx/       SDL3 window/renderer, texture + YUV textures, text atlas, themes
  ui/        all screens, image/video viewer, dialogs, pure view/search/sort/session models
tests/       crypto/ gfx/ image/ platform/ ui/ vault/ media/ + test_framework.h, test_main.cpp
vendor/      git submodules — pinned versions, build mechanics, CI matrix in mem:tech_stack
```

- `app/` + `platform/` — App lifecycle, event loop, vault ownership + auto-lock, config-dir
  persistence, hardening, diagnostics: **`mem:module/app`**.
- `crypto/` + `vault/` — storage & security core: the `.osv` format, index tree, chunk
  framing, transfer/combine/search, node-name safety: **`mem:module/vault`**.
- `image/` + `media/` — all decode: image codecs + thumbnails, FFmpeg video/audio, hardware
  accel: **`mem:module/media`**.
- `gfx/` — SDL rendering primitives, texture caches, text, themes: **`mem:module/gfx`**.
- `ui/` — every Screen, the image/video viewer, all dialogs, and the pure SDL-free
  view/search/sort/session models: **`mem:module/ui`**.

## Project-wide invariants (NEVER violate)

1. No decrypted bytes to disk — only `mlock`'d heap buffers.
   EXCEPTION (documented, gated): `src/ui/export.*` deliberately writes decrypted originals to
   disk on explicit user consent (selection-only, never thumbnails, buffer wiped right after
   write). No other path may write plaintext.
2. All key/KEK/password buffers wiped with `crypto_wipe` before free.
3. Every XChaCha20-Poly1305 encrypt call uses a fresh 24-byte CSPRNG nonce.
4. Tag verified before any plaintext bytes are consumed.
5. Keys, passwords, decrypted content must never appear in log output.
6. A vault file is untrusted input (portable/shareable); node names are path components, never
   paths — validate on ingress (`vault::is_safe_node_name`), repair on import
   (`vault::sanitize_node_name`), containment-check on export (`ui::export_path_within`); never
   build `dest_dir / node.name` directly (CWE-22). See `mem:module/vault` safe_name.*.

## Key hierarchy

`KEK = Argon2id(password [‖ keyfile], salt)` → unwraps a random 32-byte master key.
All data/thumbnail/index chunks are encrypted with the master key + a fresh nonce per chunk.

## Vault write atomicity

Append chunks → fsync → write index to inactive slot → fsync → flip `active_slot` → fsync.
(Full 3-phase detail in `mem:module/vault` index_io.*.)

## Other memories
- Tech stack, pinned deps, build mechanics, CI matrix: `mem:tech_stack`
- Build / run / test commands: `mem:suggested_commands`
- Code conventions (naming, error handling, headers, testing policy): `mem:conventions`
- Task-completion checklist: `mem:task_completion`
- Full `.osv` binary layout (header/chunk/index byte fields): `mem:vault_format`
- UI/UX specification (screen designs, F1 help convention): `mem:ui_spec`
- How this memory graph is meant to be structured/maintained: `mem:memory_maintenance`
