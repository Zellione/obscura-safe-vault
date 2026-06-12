# obscura-safe-vault — Core

Multi-platform encrypted photo gallery. Single `.osv` vault file. Decrypted data lives only in `mlock`'d heap — never written to disk.

## Source map

```
src/
  app/         main.cpp, app.{h,cpp}          — state machine + SDL event loop
  crypto/      aead.*, kdf.*, random.*,        — Monocypher wrappers
               secure_mem.h, crypto.h
  vault/       vault.*, header.*, index.*,     — .osv container format
               chunk_store.*, byte_io.h, file_util.h
  image/       decode.*, thumbnail.*           — stb_image decode, thumb gen
  gfx/         window.*, renderer.*,           — SDL3 window/renderer, texture cache,
               texture_cache.*, text.*         — stb_truetype text atlas
  ui/          unlock_screen.*, gallery_grid.* — UI screens
               image_viewer.*, widgets.*
               input.*, nav_model.*, viewer_model.h
               passphrase.*, screen.h
               secure_text_field.*, unlock_logic.*
  platform/    paths.*, file_dialog.*          — config dirs, SDL file dialogs
tests/
  crypto/ gfx/ image/ platform/ ui/ vault/
  test_framework.h  test_main.cpp
vendor/
  SDL3/         git submodule, built by setup.sh (cmake)
  monocypher/   git submodule, single .c compiled by premake
  stb/          git submodule, header-only
assets/fonts/   bundled OFL font for stb_truetype
```

## Project-wide invariants (NEVER violate)

1. No decrypted bytes to disk — only `mlock`'d heap buffers.
2. All key/KEK/password buffers wiped with `crypto_wipe` before free.
3. Every XChaCha20-Poly1305 encrypt call uses a fresh 24-byte CSPRNG nonce.
4. Tag verified before any plaintext bytes are consumed.
5. Keys, passwords, decrypted content must never appear in log output.

## Key hierarchy

`KEK = Argon2id(password [‖ keyfile], salt)` → unwraps random 32-byte master key.
All data/thumbnail/index chunks encrypted with master key + fresh nonce per chunk.

## Vault write atomicity

Append chunks → fsync → write index to inactive slot → fsync → flip `active_slot` → fsync.

## Tech stack details: `mem:tech_stack`
## Build/run/test commands: `mem:suggested_commands`
## Code conventions: `mem:conventions`
## Task-completion checklist: `mem:task_completion`
