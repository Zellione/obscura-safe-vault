# Phase 5 — UI Layer (unlock screen + gallery grid)

**Date:** 2026-06-10
**Status:** Approved design, pending implementation plan
**Builds on:** Phases 0–4 (window/renderer, crypto core, vault container, image decode/thumbnails, graphics layer)

---

## Goal

Make the vault usable from the keyboard and mouse: unlock or create a vault, then
browse a free-nesting gallery tree as a tile grid with a breadcrumb bar, import
images through the native file dialog (decode → thumbnail → encrypted store), and
create sub-galleries. The full-image viewer and thumbnail strip are **Phase 6**.

### In scope

- `ui::Screen` interface + an `App` screen state machine (`Locked` / `Browsing`).
- Unlock screen: unlock existing vault, create new vault, optional keyfile, "open
  other vault…".
- Gallery grid: breadcrumb navigation, folder + thumbnail tiles, keyboard + mouse.
- Full import pipeline: file dialog (multi-select) → decode → `make_thumbnail` →
  `Vault::add_image` → `commit`.
- Create sub-gallery from the UI.
- `platform/paths` (config dir, default vault path) and `platform/file_dialog`
  (async `SDL_ShowOpenFileDialog` wrapper).
- `ui::input` action mapping.

### Out of scope (deferred)

- **Passphrase-strength meter and random-passphrase generator → Phase 7.**
  (Resolves the CLAUDE.md contradiction between the UI/UX section and the
  deferred-decisions table in favour of the table.)
- **Image viewer / zoom-pan / thumbnail strip → Phase 6.** Selecting an image in
  the grid is a no-op in Phase 5.
- **Image deletion from the grid.** `Vault::remove_image` exists but stays unused
  until a later phase; keeps Phase 5 focused on browse / create / import.
- Asynchronous (threaded) Argon2id derivation — Phase 5 derives synchronously.

---

## Architecture: screen state machine

`App` owns the long-lived subsystems and exactly one active screen. Screens are
swappable and request transitions through a small navigation value that `App`
polls each frame. Screens hold **references** to shared subsystems — they never
own the vault or any key material.

### `ui::Screen` (new `src/ui/screen.h`)

```cpp
namespace ui {

enum class NavKind { None, ToUnlock, ToGallery, Quit };

struct Nav {
    NavKind kind = NavKind::None;
    // ToGallery carries no payload: the unlocked vault already lives in App.
};

class Screen {
public:
    virtual ~Screen() = default;

    virtual void on_enter() {}
    virtual void on_exit()  {}

    virtual void handle_event(const SDL_Event& e) = 0;
    virtual void update(double dt) {}
    virtual void render(gfx::Renderer& r) = 0;

    // App polls this after update/render and consumes any transition request.
    [[nodiscard]] Nav take_nav() noexcept { Nav n = nav_; nav_ = {}; return n; }

protected:
    void request(NavKind k) noexcept { nav_ = Nav{k}; }

private:
    Nav nav_{};
};

} // namespace ui
```

### `App` (revised `src/app/app.{h,cpp}`)

`App` owns: `gfx::Window`, `gfx::FontAtlas`, `gfx::TextureCache` (created after the
renderer exists), `vault::Vault vault_`, and `std::unique_ptr<ui::Screen> screen_`.

`State` becomes real:

```cpp
enum class State { Locked, Browsing }; // Viewing reserved for Phase 6
```

Main loop (`App::run`):

```
while (running_):
    while (window_.poll_event(e)):
        if e.type == SDL_EVENT_QUIT or window-close: running_ = false
        else: screen_->handle_event(e)
    file_dialog_.pump()                 // deliver any async dialog result
    screen_->update(dt)
    window_.begin_frame(...)
    screen_->render(renderer)
    window_.end_frame()
    switch (screen_->take_nav().kind):
        ToGallery: swap to GalleryGrid (vault_ is unlocked)
        ToUnlock:  vault_.lock(); swap to UnlockScreen
        Quit:      running_ = false
        None:      (continue)
```

Screen construction injects references: `UnlockScreen(window_, font_, vault_,
file_dialog_, default_vault_path_)` and `GalleryGrid(window_, font_, vault_,
texture_cache_, file_dialog_)`.

### `gfx::Window` change

Remove the "Escape quits the app" behaviour from event handling so Esc is free for
"go up / back". Replace `process_events(bool& quit)` with a thin passthrough:

```cpp
[[nodiscard]] bool poll_event(SDL_Event& out); // wraps SDL_PollEvent; returns false when the queue is empty
```

`App` decides what window-close / quit means. (`begin_frame`/`end_frame` are
unchanged.) The old `process_events` is removed; the Phase 4 demo `run()` body is
replaced by the screen loop above.

---

## Components

### `platform/paths.{h,cpp}`

```cpp
namespace platform {
// SDL_GetPrefPath("ObscuraSafeVault", "ObscuraSafeVault") — cross-platform,
// creates the directory, returns a char* the caller must SDL_free. Wrapped to
// return a std::filesystem::path. Empty path on failure (logged).
[[nodiscard]] std::filesystem::path config_dir();

// config_dir() / "vault.osv"
[[nodiscard]] std::filesystem::path default_vault_path();
}
```

Using `SDL_GetPrefPath` avoids per-OS `#ifdef`s and matches the project's
"SDL covers the platform layer" principle. The only headlessly-testable piece is
the join logic (`default_vault_path` = `config_dir` + filename); `config_dir`
itself wraps SDL and is exercised via the dialog/integration paths.

### `platform/file_dialog.{h,cpp}`

Wraps the async `SDL_ShowOpenFileDialog`. The SDL callback signature is
`void(void* userdata, const char* const* filelist, int filter)` and **may run on
a non-main thread** depending on the backend, so results land in a
mutex-guarded slot and are delivered to the main thread by polling.

```cpp
namespace platform {

class FileDialog {
public:
    // Filters built from SDL_DialogFileFilter{name, "osv"} etc.
    void open_vault(SDL_Window* parent);                 // *.osv, single
    void open_images(SDL_Window* parent);                // jpg/png/gif/bmp/tga/hdr, multi
    void open_keyfile(SDL_Window* parent);               // any, single

    // Called once per frame by App; no-op if nothing pending. Moves a completed
    // result from the callback slot so take_result() can return it.
    void pump();

    // Non-empty exactly once after a successful selection; empty if cancelled.
    [[nodiscard]] std::optional<std::vector<std::string>> take_result() noexcept;

    [[nodiscard]] bool busy() const noexcept;            // a dialog is open

private:
    static void SDLCALL on_files(void* ud, const char* const* list, int filter);
    std::mutex mtx_;
    // pending/ready/idle state + collected paths guarded by mtx_
};

} // namespace platform
```

`filelist == nullptr` signals an SDL error; a `nullptr`-terminated empty list
signals user cancellation. Both yield "no result"; an error is logged with a
`[Platform]` prefix. The dialog is non-modal w.r.t. our render loop — `busy()`
lets screens disable input while it is open.

### `ui/input.h`

```cpp
namespace ui {
enum class InputAction {
    None,
    NavLeft, NavRight, NavUp, NavDown,
    Select,        // Enter / Space
    Back,          // Backspace / Esc
    Import,        // I
    NewGallery,    // N
    Quit,          // (App-level; window close)
    // Phase 6: ZoomIn, ZoomOut, PanLeft, ...
};

// Pure mapping — no SDL state, no globals. Unit-tested with key/modifier KATs.
[[nodiscard]] InputAction map_key(SDL_Keycode key, SDL_Keymod mods) noexcept;
}
```

Screens that are typing text (password field, gallery-name prompt) consume
`SDL_EVENT_TEXT_INPUT` / editing keys directly and do **not** route through
`map_key`, so letters like `i`/`n` never trigger Import/NewGallery while typing.

### `ui/widgets.{h,cpp}`

Reusable draw helpers plus **pure** layout / hit-test math (the unit-tested core;
draws are thin SDL wrappers, untested like the existing gfx draw code):

```cpp
namespace ui {
// Pure
[[nodiscard]] bool      point_in_rect(float x, float y, const SDL_FRect& r) noexcept;
[[nodiscard]] SDL_FRect grid_cell_rect(int index, int cols, float cell, float gap,
                                       float origin_x, float origin_y) noexcept;
[[nodiscard]] int       grid_columns(float avail_w, float cell, float gap) noexcept;
[[nodiscard]] int       grid_hit(float mx, float my, int count, int cols,
                                  float cell, float gap, float ox, float oy) noexcept; // -1 = miss

// Draw (thin)
struct Button { SDL_FRect rect; std::string label; };
void draw_button(gfx::Renderer&, gfx::FontAtlas&, const Button&, bool hover, bool active);
void draw_text_field(gfx::Renderer&, gfx::FontAtlas&, const SDL_FRect&,
                     std::string_view shown, bool focused);
}
```

Breadcrumb path ↔ segment conversion is pure and tested (see `NavModel`).

### `ui/secure_text_field.{h,cpp}` — password entry

Password bytes never live in an unlocked `std::string` (invariant #2). Because
`crypto::SecureBytes::resize` **wipes and reallocates** (no append, and growth
would leave a plaintext copy behind), the field allocates a **fixed-capacity**
secure buffer once and tracks a logical length:

```cpp
namespace ui {
class SecureTextField {
public:
    explicit SecureTextField(size_t capacity = 512); // one mlock'd SecureBytes
    void push_utf8(std::string_view text); // append SDL_EVENT_TEXT_INPUT bytes (clamp at capacity)
    void backspace();                      // drop last byte
    void clear() noexcept;                 // wipe + length = 0
    [[nodiscard]] size_t length() const noexcept;
    [[nodiscard]] std::span<const uint8_t> bytes() const noexcept; // for unlock/create + equality
    // Rendered as length() mask glyphs; the bytes are never stringified.
};
}
```

Overflow past capacity is ignored (a 512-byte passphrase is far beyond practical).
Tests assert append/backspace/clear semantics and that the backing memory is zero
after `clear()` (mirroring the existing `test_secure_*` pattern).

### `ui/unlock_screen.{h,cpp}` (`ui::Screen`)

One screen, two modes, toggled by a button:

- **Unlock** (default when `default_vault_path()` exists): password field (masked),
  optional keyfile (chosen via `FileDialog::open_keyfile`), "Open other vault…"
  (`open_vault`) to target a different `.osv`, error line.
- **Create** (default when no vault exists, or toggled): password + confirm
  (both masked) + optional keyfile.

Submit logic is factored into a **pure, headlessly-tested** function:

```cpp
enum class SubmitAction { None, Unlock, Create };
struct SubmitDecision { SubmitAction action; const char* error; }; // error==nullptr on ok
[[nodiscard]] SubmitDecision decide_submit(bool create_mode,
                                           std::span<const uint8_t> pw,
                                           std::span<const uint8_t> confirm);
// Create: pw non-empty AND pw == confirm, else error. Unlock: pw non-empty.
```

On `Unlock`: `Vault::open(path)` → `vault_.unlock(pw, keyfile)`.
`VaultResult::AuthFailed` → "Wrong password or keyfile."; `BadFormat`/`IoError`
→ corresponding messages. Success → wipe fields → `request(ToGallery)`.

On `Create`: `Vault::create(path, pw, keyfile, crypto::DEFAULT_KDF_PARAMS, vault_)`
→ success → wipe fields → `request(ToGallery)`.

Argon2id (64 MiB / 3 passes) runs **synchronously** on submit. The screen draws a
"Working…" label for the frame the derive runs; threading is deferred. After
either path, all `SecureTextField`s are cleared (wiped).

### `ui/gallery_grid.{h,cpp}` (`ui::Screen`)

Owns a **pure** `NavModel` (separated from rendering for testability):

```cpp
namespace ui {
class NavModel {
public:
    void enter(std::string segment);     // descend
    bool up();                           // pop; false if already at root
    [[nodiscard]] std::string path() const;      // "a/b/c" (root => "")
    [[nodiscard]] std::span<const std::string> segments() const; // breadcrumb
    void set_count(int n);               // children count for clamping
    void move(int delta);                // selection +/- with clamp
    [[nodiscard]] int  selected() const;
    void reset_selection();
};
// Free helpers, tested: split_path("a/b") -> {"a","b"}, join_path({...}) -> "a/b".
}
```

Per frame the screen calls `vault_.list(nav_.path())` and renders:

- **Gallery tile**: filled rounded rect + folder marker + name (ellipsised to cell
  width via `FontAtlas::measure`).
- **Image tile**: `vault_.read_thumbnail(node, sb)` → `decode_from_memory(sb.as_span())`
  → `texture_cache_.get_or_upload(key, img)` → `draw_image` fit into the cell.
  **Cache key** = `node.meta.data_offset` (unique and stable per stored chunk).
- **Breadcrumb bar** across the top from `nav_.segments()`; clickable segments pop
  the stack to that depth.
- **Selection highlight** border on the current cell.

Input:

- Arrows → `NavModel::move` (NavUp/Down move by one row = `cols`).
- `Select` (Enter/Space) on a gallery → `nav_.enter(name)`; on an image → no-op
  (Phase 6).
- `Back` (Backspace/Esc) → `nav_.up()`; if already at root → `vault_.lock()` +
  `request(ToUnlock)`.
- Mouse: `grid_hit` sets selection / opens on click; breadcrumb clicks pop.
- `Import` (`I`): only when the current gallery is a valid import target (empty or
  already image-holding — never one with sub-galleries). Opens
  `FileDialog::open_images`. On result, for each path: read bytes (plain file
  read — these are *plaintext sources the user chose*, not vault secrets) →
  `decode_from_memory` (skip + log undecodable) → `make_thumbnail` →
  `vault_.add_image(path, bytes, filename)`; after the batch, the vault has already
  committed per `add_image` (one index commit per call). `AlreadyExists` /
  `InvalidArg` surface as an error line. Grid refreshes.
- `NewGallery` (`N`): opens an inline **plaintext** name prompt (a small modal
  reusing `draw_text_field`; text via `SDL_EVENT_TEXT_INPUT`). On confirm →
  `vault_.create_gallery(join(path, name))`; rejects when the current gallery holds
  images (`InvalidArg`). Refreshes.

The decrypted thumbnail JPEG arrives in `SecureBytes`; decoding produces a
transient `image::ImageData` (plain `std::vector`, never written to disk) — this
matches the Phase 3/4 decision that decoded pixels are transient and need no
mlock, while upholding invariant #1 (no plaintext to disk).

---

## Data flow

```
Unlock/Create screen
   └─ Vault::open+unlock / Vault::create  ──>  App.vault_ (UNLOCKED, master key mlock'd)
        └─ request(ToGallery)  ──>  App swaps to GalleryGrid(vault_, texture_cache_)

GalleryGrid
   browse:  Vault::list(path) ─> tiles
   thumb:   Vault::read_thumbnail ─> SecureBytes ─> decode ─> TextureCache(data_offset) ─> draw
   import:  FileDialog ─> file bytes ─> decode ─> make_thumbnail ─> Vault::add_image (+commit)
   newdir:  name prompt ─> Vault::create_gallery
   back at root: Vault::lock() ─> request(ToUnlock)
```

---

## Error handling & security

- No exceptions. Every `VaultResult` / `bool` is checked; failures become UI error
  lines and a `[UI]` / `[Platform]` stderr log.
- **Never log** passwords, keys, keyfile contents, or decrypted pixels.
- Password input stays in mlock'd `SecureTextField`; wiped after submit and on
  screen exit.
- `App::shutdown`: `vault_.lock()` (wipes master key) → `texture_cache_.clear()` →
  `font_.release_texture()` → `window_.shutdown()` (reverse-init order; textures
  released before the renderer that owns them is destroyed).
- File-dialog callback is treated as cross-thread: guarded by a mutex, delivered
  on the main thread via `pump()`.

---

## Testing (headless, matching the existing pattern)

Pure logic is unit-tested; SDL draw calls are not (consistent with the Phase 4
gfx tests, which test layout math + headless bake only).

- `tests/ui/test_input.cpp` — `map_key` KATs (arrows, Enter/Space, Esc/Backspace,
  I, N, modifiers).
- `tests/ui/test_widgets.cpp` — `point_in_rect`, `grid_columns`, `grid_cell_rect`,
  `grid_hit` (incl. misses), `split_path`/`join_path`.
- `tests/ui/test_nav_model.cpp` — enter/up/path/segments, selection clamp at
  bounds, root-up returns false.
- `tests/ui/test_secure_text_field.cpp` — push/backspace/clear/length, capacity
  clamp, and **memory zeroed after clear** (read raw bytes like `test_secure_*`).
- `tests/ui/test_unlock_logic.cpp` — `decide_submit` truth table (create mismatch,
  empty password, valid unlock/create).
- `tests/platform/test_paths.cpp` — `default_vault_path` = `config_dir` + filename
  join logic (config_dir stubbed/checked for suffix).

`premake5.lua` already globs `src/**` and the test runner globs `tests/**`; the new
files are picked up automatically. Integration of the full import round-trip
(create vault → create gallery → add image → re-list → read thumbnail) reuses the
existing `tests/vault` round-trip helpers and needs no SDL.

---

## File summary

**New**

```
src/ui/screen.h
src/ui/input.h                    (replaces the Phase 5 stub)
src/ui/widgets.{h,cpp}
src/ui/secure_text_field.{h,cpp}
src/ui/unlock_screen.{h,cpp}
src/ui/gallery_grid.{h,cpp}
src/platform/paths.{h,cpp}        (paths.h replaces the stub; paths.cpp new)
src/platform/file_dialog.{h,cpp}
tests/ui/test_input.cpp
tests/ui/test_widgets.cpp
tests/ui/test_nav_model.cpp
tests/ui/test_secure_text_field.cpp
tests/ui/test_unlock_logic.cpp
tests/platform/test_paths.cpp
```

**Modified**

```
src/app/app.h     — real State enum, owns TextureCache + Vault + active Screen
src/app/app.cpp   — screen loop replaces the Phase 4 demo; shutdown wipes vault
src/gfx/window.h  — poll_event() replaces process_events(); no Esc-quit
src/gfx/window.cpp
```

No premake or script changes required (wildcard file globs already cover `src/**`
and `tests/**`).
