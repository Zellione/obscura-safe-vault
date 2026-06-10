# Phase 5 — UI Layer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the vault usable: an unlock/create screen and a keyboard+mouse gallery grid with breadcrumb navigation, image import, and sub-gallery creation, all driven by a `ui::Screen` state machine in `App`.

**Architecture:** `App` owns the window, font, texture cache, file dialog, and the single `vault::Vault`, plus one active `std::unique_ptr<ui::Screen>`. Screens hold references to those subsystems (never owning keys), handle raw SDL events, and request transitions via a small `Nav` value `App` consumes each frame. Pure logic (input mapping, layout math, nav model, password buffer, submit validation, paths) lives in dedicated files unit-tested headlessly; SDL-heavy screens/dialog/window are verified by building and running the app.

**Tech Stack:** C++20, SDL3, Monocypher (`crypto::SecureBytes`), premake5→Ninja, the project's custom `tests/test_framework.h` harness.

---

## Conventions for every task

- **Build the app:** `scripts/gen.sh && scripts/build.sh` → binary at `build/bin/Debug/osv`.
- **Run tests:** `scripts/test.sh` (regenerates premake, builds `osv_tests`, runs it). `scripts/test.sh --asan` for sanitizers.
- **Naming/style:** `snake_case`, `PascalCase` types, trailing-underscore members, `[[nodiscard]]`, no exceptions, `[Module]` stderr prefixes, `#pragma once`.
- **premake note:** the `osv` app globs `src/**.cpp` (new files picked up automatically). The `osv_tests` project lists sources **explicitly**, so every new *testable* `src` unit must be added to its `files {}` block (done inside the relevant task). SDL-heavy screen/dialog/window/app sources are **not** added to `osv_tests`.

---

## File structure

**New — testable (added to `osv_tests` in premake):**
- `src/platform/paths.{h,cpp}` — config dir, default vault path, `read_file`.
- `src/ui/input.{h,cpp}` — `map_key`.
- `src/ui/nav_model.{h,cpp}` — `NavModel` + `split_path`/`join_path`.
- `src/ui/secure_text_field.{h,cpp}` — mlock'd password buffer.
- `src/ui/unlock_logic.{h,cpp}` — `decide_submit`.
- `src/ui/widgets.{h,cpp}` — pure layout/hit-test + thin draw helpers.

**New — not unit-tested (built only in `osv`):**
- `src/ui/screen.h` — `Screen` interface + `Nav`.
- `src/platform/file_dialog.{h,cpp}` — async `SDL_ShowOpenFileDialog` wrapper.
- `src/ui/unlock_screen.{h,cpp}`, `src/ui/gallery_grid.{h,cpp}` — the two screens.

**Modified:**
- `src/gfx/window.{h,cpp}` — `poll_event()` replaces `process_events()` (no Esc-quit).
- `src/app/app.{h,cpp}` — screen state machine replaces the Phase 4 demo.
- `premake5.lua` — add the six testable sources to `osv_tests`.

**Tests:** `tests/platform/test_paths.cpp`, `tests/ui/test_input.cpp`, `tests/ui/test_nav_model.cpp`, `tests/ui/test_secure_text_field.cpp`, `tests/ui/test_unlock_logic.cpp`, `tests/ui/test_widgets.cpp`.

**Deltas from the spec (intentional, better decomposition):** `NavModel` and `decide_submit` are their own files (testable in isolation); `FileDialog::pump()` dropped in favour of polling `take_result()`; import passes raw bytes straight to `add_image` (it decodes/thumbnails internally); added tested helpers `platform::read_file` and `ui::fit_rect`; `premake5.lua` **is** edited (corrects the spec's "no premake changes" note for the test target).

---

## Task 1: platform/paths

**Files:**
- Create: `src/platform/paths.cpp` (and replace stub `src/platform/paths.h`)
- Modify: `premake5.lua` (add `src/platform/paths.cpp` to `osv_tests`)
- Test: `tests/platform/test_paths.cpp`

- [ ] **Step 1: Write the failing test**

`tests/platform/test_paths.cpp`:
```cpp
#include "test_framework.h"

#include <filesystem>

#include "platform/paths.h"

TEST(paths_default_vault_filename)
{
    CHECK(platform::default_vault_path().filename() == "vault.osv");
}

TEST(paths_default_vault_under_config_dir)
{
    auto cfg = platform::config_dir();
    if (!cfg.empty())
        CHECK(platform::default_vault_path().parent_path() == cfg);
}

TEST(paths_read_file_roundtrip)
{
    auto tmp = std::filesystem::temp_directory_path() / "osv_paths_test.bin";
    const std::vector<uint8_t> data{1, 2, 3, 4, 250};
    {
        std::FILE* f = std::fopen(tmp.string().c_str(), "wb");
        REQUIRE(f != nullptr);
        std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
    }
    auto got = platform::read_file(tmp);
    REQUIRE(got.has_value());
    CHECK_BYTES_EQ(std::span<const uint8_t>(*got), std::span<const uint8_t>(data));
    std::filesystem::remove(tmp);
}

TEST(paths_read_file_missing_returns_nullopt)
{
    CHECK_FALSE(platform::read_file("/no/such/osv/file.xyz").has_value());
}
```

- [ ] **Step 2: Replace the header**

`src/platform/paths.h` (replace the whole stub):
```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace platform {

// Per-user data directory (created if needed). Empty path on failure.
[[nodiscard]] std::filesystem::path config_dir();

// config_dir() / "vault.osv"  (just the filename if config_dir() is empty).
[[nodiscard]] std::filesystem::path default_vault_path();

// Read an entire file into a byte vector. nullopt if it cannot be opened/read.
[[nodiscard]] std::optional<std::vector<uint8_t>>
read_file(const std::filesystem::path& path);

} // namespace platform
```

- [ ] **Step 3: Write the implementation**

`src/platform/paths.cpp`:
```cpp
#include "platform/paths.h"

#include <SDL3/SDL.h>

#include <cstdio>

namespace platform {

std::filesystem::path config_dir()
{
    char* pref = SDL_GetPrefPath("ObscuraSafeVault", "ObscuraSafeVault");
    if (!pref) return {};
    std::filesystem::path p{pref};
    SDL_free(pref);
    return p;
}

std::filesystem::path default_vault_path()
{
    auto dir = config_dir();
    return dir.empty() ? std::filesystem::path{"vault.osv"} : dir / "vault.osv";
}

std::optional<std::vector<uint8_t>> read_file(const std::filesystem::path& path)
{
    std::FILE* f = std::fopen(path.string().c_str(), "rb");
    if (!f) return std::nullopt;

    std::vector<uint8_t> buf;
    uint8_t chunk[64 * 1024];
    size_t n;
    while ((n = std::fread(chunk, 1, sizeof chunk, f)) > 0)
        buf.insert(buf.end(), chunk, chunk + n);

    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    if (!ok) return std::nullopt;
    return buf;
}

} // namespace platform
```

- [ ] **Step 4: Wire into the test build**

In `premake5.lua`, inside `project "osv_tests"`'s `files { ... }` block, after the `src/image/*` entries add:
```lua
        "src/platform/paths.cpp",
        "src/ui/input.cpp",
        "src/ui/nav_model.cpp",
        "src/ui/secure_text_field.cpp",
        "src/ui/unlock_logic.cpp",
        "src/ui/widgets.cpp",
```
(All six are added now so later tasks need no further premake edits. The referenced `.cpp` files are created in their tasks; until then they don't exist — **so build `osv_tests` only after a unit's source file exists.** This task created `paths.cpp`; the others are stubbed at the top of each later task before its first test run.)

> If your runner builds tests between tasks, instead add only `"src/platform/paths.cpp"` now and add each remaining line at Step "wire into the test build" of its task. Either approach works; pick one and be consistent.

- [ ] **Step 5: Run tests**

Run: `scripts/test.sh`
Expected: `paths_*` tests PASS (and all pre-existing tests still PASS).

- [ ] **Step 6: Commit**

```bash
git add src/platform/paths.h src/platform/paths.cpp tests/platform/test_paths.cpp premake5.lua
git commit -m "Phase 5: platform/paths (config dir, default vault path, read_file)"
```

---

## Task 2: ui/input — key → action mapping

**Files:**
- Create: `src/ui/input.cpp` (replace stub `src/ui/input.h`)
- Test: `tests/ui/test_input.cpp`

- [ ] **Step 1: Write the failing test**

`tests/ui/test_input.cpp`:
```cpp
#include "test_framework.h"

#include <SDL3/SDL.h>

#include "ui/input.h"

TEST(input_arrows)
{
    using ui::InputAction;
    CHECK(ui::map_key(SDLK_LEFT,  SDL_KMOD_NONE) == InputAction::NavLeft);
    CHECK(ui::map_key(SDLK_RIGHT, SDL_KMOD_NONE) == InputAction::NavRight);
    CHECK(ui::map_key(SDLK_UP,    SDL_KMOD_NONE) == InputAction::NavUp);
    CHECK(ui::map_key(SDLK_DOWN,  SDL_KMOD_NONE) == InputAction::NavDown);
}

TEST(input_select_back_commands)
{
    using ui::InputAction;
    CHECK(ui::map_key(SDLK_RETURN,    SDL_KMOD_NONE) == InputAction::Select);
    CHECK(ui::map_key(SDLK_KP_ENTER,  SDL_KMOD_NONE) == InputAction::Select);
    CHECK(ui::map_key(SDLK_SPACE,     SDL_KMOD_NONE) == InputAction::Select);
    CHECK(ui::map_key(SDLK_BACKSPACE, SDL_KMOD_NONE) == InputAction::Back);
    CHECK(ui::map_key(SDLK_ESCAPE,    SDL_KMOD_NONE) == InputAction::Back);
    CHECK(ui::map_key(SDLK_I,         SDL_KMOD_NONE) == InputAction::Import);
    CHECK(ui::map_key(SDLK_N,         SDL_KMOD_NONE) == InputAction::NewGallery);
}

TEST(input_unmapped_is_none)
{
    CHECK(ui::map_key(SDLK_F5, SDL_KMOD_NONE) == ui::InputAction::None);
}
```

- [ ] **Step 2: Replace the header**

`src/ui/input.h` (replace the whole stub):
```cpp
#pragma once

#include <SDL3/SDL.h>

namespace ui {

enum class InputAction {
    None,
    NavLeft, NavRight, NavUp, NavDown,
    Select,      // Enter / KP-Enter / Space
    Back,        // Backspace / Escape
    Import,      // I
    NewGallery,  // N
    // Phase 6: ZoomIn, ZoomOut, ...
};

// Pure mapping of a key (modifiers reserved for later phases) to a UI action.
[[nodiscard]] InputAction map_key(SDL_Keycode key, SDL_Keymod mods) noexcept;

} // namespace ui
```

- [ ] **Step 3: Write the implementation**

`src/ui/input.cpp`:
```cpp
#include "ui/input.h"

namespace ui {

InputAction map_key(SDL_Keycode key, SDL_Keymod /*mods*/) noexcept
{
    switch (key) {
        case SDLK_LEFT:      return InputAction::NavLeft;
        case SDLK_RIGHT:     return InputAction::NavRight;
        case SDLK_UP:        return InputAction::NavUp;
        case SDLK_DOWN:      return InputAction::NavDown;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:     return InputAction::Select;
        case SDLK_BACKSPACE:
        case SDLK_ESCAPE:    return InputAction::Back;
        case SDLK_I:         return InputAction::Import;
        case SDLK_N:         return InputAction::NewGallery;
        default:             return InputAction::None;
    }
}

} // namespace ui
```

- [ ] **Step 4: Run tests** — `scripts/test.sh` → `input_*` PASS.
- [ ] **Step 5: Commit**

```bash
git add src/ui/input.h src/ui/input.cpp tests/ui/test_input.cpp
git commit -m "Phase 5: ui::map_key input-action mapping"
```

---

## Task 3: ui/nav_model — breadcrumb path + selection

**Files:**
- Create: `src/ui/nav_model.h`, `src/ui/nav_model.cpp`
- Test: `tests/ui/test_nav_model.cpp`

- [ ] **Step 1: Write the failing test**

`tests/ui/test_nav_model.cpp`:
```cpp
#include "test_framework.h"

#include "ui/nav_model.h"

TEST(nav_split_join_roundtrip)
{
    auto segs = ui::split_path("alpha/beta/gamma");
    REQUIRE(segs.size() == 3);
    CHECK(segs[0] == "alpha");
    CHECK(segs[2] == "gamma");
    CHECK(ui::join_path(segs) == "alpha/beta/gamma");
    CHECK(ui::split_path("").empty());
    CHECK(ui::join_path({}) == "");
}

TEST(nav_enter_up_path)
{
    ui::NavModel m;
    CHECK(m.path() == "");
    CHECK_FALSE(m.up());            // already at root
    m.enter("photos");
    m.enter("2024");
    CHECK(m.path() == "photos/2024");
    REQUIRE(m.up());
    CHECK(m.path() == "photos");
}

TEST(nav_selection_clamp)
{
    ui::NavModel m;
    m.set_count(3);
    CHECK_EQ(m.selected(), 0);
    m.move(-1);
    CHECK_EQ(m.selected(), 0);     // clamp low
    m.move(5);
    CHECK_EQ(m.selected(), 2);     // clamp high
    m.select(1);
    CHECK_EQ(m.selected(), 1);
    m.set_count(0);
    CHECK_EQ(m.selected(), 0);     // empty resets
}
```

- [ ] **Step 2: Write the header**

`src/ui/nav_model.h`:
```cpp
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

// Split "a/b/c" into {"a","b","c"} (empty segments dropped). join_path is inverse.
[[nodiscard]] std::vector<std::string> split_path(std::string_view path);
[[nodiscard]] std::string              join_path(std::span<const std::string> segs);

// Current location in the gallery tree (a stack of names) plus a clamped
// selection index over the current gallery's children. Pure; no SDL.
class NavModel {
public:
    void enter(std::string segment);   // descend; selection resets to 0
    bool up() noexcept;                // ascend; false if already at root

    [[nodiscard]] std::string                  path() const;       // "" at root
    [[nodiscard]] std::span<const std::string> segments() const noexcept { return segments_; }

    void set_count(int n) noexcept;    // child count; re-clamps selection
    void move(int delta) noexcept;     // selection += delta, clamped
    void select(int index) noexcept;   // selection = index, clamped
    [[nodiscard]] int selected() const noexcept { return selected_; }
    [[nodiscard]] int count() const noexcept { return count_; }

private:
    void clamp() noexcept;

    std::vector<std::string> segments_;
    int                      count_    = 0;
    int                      selected_ = 0;
};

} // namespace ui
```

- [ ] **Step 3: Write the implementation**

`src/ui/nav_model.cpp`:
```cpp
#include "ui/nav_model.h"

namespace ui {

std::vector<std::string> split_path(std::string_view path)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= path.size()) {
        size_t slash = path.find('/', start);
        if (slash == std::string_view::npos) {
            if (start < path.size()) out.emplace_back(path.substr(start));
            break;
        }
        if (slash > start) out.emplace_back(path.substr(start, slash - start));
        start = slash + 1;
    }
    return out;
}

std::string join_path(std::span<const std::string> segs)
{
    std::string out;
    for (size_t i = 0; i < segs.size(); ++i) {
        if (i) out += '/';
        out += segs[i];
    }
    return out;
}

void NavModel::enter(std::string segment)
{
    segments_.push_back(std::move(segment));
    selected_ = 0;
    count_    = 0;
}

bool NavModel::up() noexcept
{
    if (segments_.empty()) return false;
    segments_.pop_back();
    selected_ = 0;
    count_    = 0;
    return true;
}

std::string NavModel::path() const { return join_path(segments_); }

void NavModel::set_count(int n) noexcept { count_ = n < 0 ? 0 : n; clamp(); }
void NavModel::move(int delta) noexcept  { selected_ += delta; clamp(); }
void NavModel::select(int index) noexcept { selected_ = index; clamp(); }

void NavModel::clamp() noexcept
{
    if (count_ <= 0)            { selected_ = 0; return; }
    if (selected_ < 0)          selected_ = 0;
    if (selected_ > count_ - 1) selected_ = count_ - 1;
}

} // namespace ui
```

- [ ] **Step 4: Run tests** — `scripts/test.sh` → `nav_*` PASS.
- [ ] **Step 5: Commit**

```bash
git add src/ui/nav_model.h src/ui/nav_model.cpp tests/ui/test_nav_model.cpp
git commit -m "Phase 5: ui::NavModel breadcrumb path + clamped selection"
```

---

## Task 4: ui/secure_text_field — mlock'd password buffer

**Files:**
- Create: `src/ui/secure_text_field.h`, `src/ui/secure_text_field.cpp`
- Test: `tests/ui/test_secure_text_field.cpp`

- [ ] **Step 1: Write the failing test**

`tests/ui/test_secure_text_field.cpp`:
```cpp
#include "test_framework.h"

#include <string_view>

#include "ui/secure_text_field.h"

static std::span<const uint8_t> bytes_of(std::string_view s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

TEST(stf_push_and_bytes)
{
    ui::SecureTextField f;
    f.push_utf8("abc");
    CHECK_EQ(f.length(), size_t{3});
    CHECK_BYTES_EQ(f.bytes(), bytes_of("abc"));
}

TEST(stf_backspace_and_clear)
{
    ui::SecureTextField f;
    f.push_utf8("hello");
    f.backspace();
    CHECK_EQ(f.length(), size_t{4});
    CHECK_BYTES_EQ(f.bytes(), bytes_of("hell"));
    f.clear();
    CHECK_EQ(f.length(), size_t{0});
    CHECK(f.empty());
    f.push_utf8("x");          // usable after clear
    CHECK_BYTES_EQ(f.bytes(), bytes_of("x"));
}

TEST(stf_capacity_clamp)
{
    ui::SecureTextField f(4);
    f.push_utf8("abcdef");
    CHECK_EQ(f.length(), size_t{4});
    CHECK_BYTES_EQ(f.bytes(), bytes_of("abcd"));
}
```

- [ ] **Step 2: Write the header**

`src/ui/secure_text_field.h`:
```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "crypto/secure_mem.h"

namespace ui {

// A text entry field whose bytes live in a single fixed-capacity mlock'd buffer
// (never an unlocked std::string), wiped on clear/destroy. Capacity is allocated
// once so growth never leaves a plaintext copy behind (CLAUDE.md invariant #2).
class SecureTextField {
public:
    explicit SecureTextField(size_t capacity = 512);

    void push_utf8(std::string_view text) noexcept;  // append, clamped at capacity
    void backspace() noexcept;                        // drop the last byte
    void clear() noexcept;                            // wipe + length = 0

    [[nodiscard]] size_t length() const noexcept { return len_; }
    [[nodiscard]] bool   empty()  const noexcept { return len_ == 0; }
    [[nodiscard]] std::span<const uint8_t> bytes() const noexcept;

private:
    crypto::SecureBytes buf_;
    size_t              cap_ = 0;
    size_t              len_ = 0;
};

} // namespace ui
```

- [ ] **Step 3: Write the implementation**

`src/ui/secure_text_field.cpp`:
```cpp
#include "ui/secure_text_field.h"

namespace ui {

SecureTextField::SecureTextField(size_t capacity) : buf_(capacity)
{
    cap_ = buf_.size();   // 0 if the secure allocation failed
}

void SecureTextField::push_utf8(std::string_view text) noexcept
{
    for (char c : text) {
        if (len_ >= cap_) break;
        buf_.data()[len_++] = static_cast<uint8_t>(c);
    }
}

void SecureTextField::backspace() noexcept
{
    if (len_ == 0) return;
    --len_;
    buf_.data()[len_] = 0;
}

void SecureTextField::clear() noexcept
{
    buf_.wipe();
    len_ = 0;
}

std::span<const uint8_t> SecureTextField::bytes() const noexcept
{
    return {buf_.data(), len_};
}

} // namespace ui
```

- [ ] **Step 4: Run tests** — `scripts/test.sh` (also run `scripts/test.sh --asan` to confirm no leaks). `stf_*` PASS.
- [ ] **Step 5: Commit**

```bash
git add src/ui/secure_text_field.h src/ui/secure_text_field.cpp tests/ui/test_secure_text_field.cpp
git commit -m "Phase 5: ui::SecureTextField mlock'd password buffer"
```

---

## Task 5: ui/unlock_logic — submit validation

**Files:**
- Create: `src/ui/unlock_logic.h`, `src/ui/unlock_logic.cpp`
- Test: `tests/ui/test_unlock_logic.cpp`

- [ ] **Step 1: Write the failing test**

`tests/ui/test_unlock_logic.cpp`:
```cpp
#include "test_framework.h"

#include <string_view>

#include "ui/unlock_logic.h"

static std::span<const uint8_t> b(std::string_view s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

TEST(unlock_empty_password_rejected)
{
    auto d = ui::decide_submit(false, b(""), b(""));
    CHECK(d.action == ui::SubmitAction::None);
    CHECK(d.error != nullptr);
}

TEST(unlock_nonempty_is_unlock)
{
    auto d = ui::decide_submit(false, b("pw"), b(""));
    CHECK(d.action == ui::SubmitAction::Unlock);
    CHECK(d.error == nullptr);
}

TEST(create_mismatch_rejected)
{
    auto d = ui::decide_submit(true, b("pw1"), b("pw2"));
    CHECK(d.action == ui::SubmitAction::None);
    CHECK(d.error != nullptr);
}

TEST(create_match_is_create)
{
    auto d = ui::decide_submit(true, b("same"), b("same"));
    CHECK(d.action == ui::SubmitAction::Create);
    CHECK(d.error == nullptr);
}
```

- [ ] **Step 2: Write the header**

`src/ui/unlock_logic.h`:
```cpp
#pragma once

#include <cstdint>
#include <span>

namespace ui {

enum class SubmitAction { None, Unlock, Create };

// action == None  =>  error != nullptr (a user-facing message).
struct SubmitDecision {
    SubmitAction action = SubmitAction::None;
    const char*  error  = nullptr;
};

// Validate an unlock/create submission. Pure; performs no crypto or I/O.
[[nodiscard]] SubmitDecision decide_submit(bool                     create_mode,
                                           std::span<const uint8_t> password,
                                           std::span<const uint8_t> confirm) noexcept;

} // namespace ui
```

- [ ] **Step 3: Write the implementation**

`src/ui/unlock_logic.cpp`:
```cpp
#include "ui/unlock_logic.h"

#include <algorithm>

namespace ui {

SubmitDecision decide_submit(bool                     create_mode,
                             std::span<const uint8_t> password,
                             std::span<const uint8_t> confirm) noexcept
{
    if (password.empty())
        return {SubmitAction::None, "Password cannot be empty."};

    if (create_mode) {
        if (!std::ranges::equal(password, confirm))
            return {SubmitAction::None, "Passwords do not match."};
        return {SubmitAction::Create, nullptr};
    }
    return {SubmitAction::Unlock, nullptr};
}

} // namespace ui
```

- [ ] **Step 4: Run tests** — `scripts/test.sh` → `unlock_*`/`create_*` PASS.
- [ ] **Step 5: Commit**

```bash
git add src/ui/unlock_logic.h src/ui/unlock_logic.cpp tests/ui/test_unlock_logic.cpp
git commit -m "Phase 5: ui::decide_submit unlock/create validation"
```

---

## Task 6: ui/widgets — layout math + draw helpers

**Files:**
- Create: `src/ui/widgets.h`, `src/ui/widgets.cpp`
- Test: `tests/ui/test_widgets.cpp`

- [ ] **Step 1: Write the failing test**

`tests/ui/test_widgets.cpp`:
```cpp
#include "test_framework.h"

#include <SDL3/SDL.h>

#include "ui/widgets.h"

TEST(widgets_point_in_rect)
{
    SDL_FRect r{10, 10, 100, 50};
    CHECK(ui::point_in_rect(10, 10, r));
    CHECK(ui::point_in_rect(60, 30, r));
    CHECK_FALSE(ui::point_in_rect(110, 30, r));   // right edge exclusive
    CHECK_FALSE(ui::point_in_rect(5, 30, r));
}

TEST(widgets_grid_columns_and_cell)
{
    CHECK_EQ(ui::grid_columns(680.0f, 160.0f, 16.0f), 4); // (680+16)/176 = 3.95 -> 3? verify below
    CHECK(ui::grid_columns(0.0f, 160.0f, 16.0f) >= 1);    // never zero

    SDL_FRect c = ui::grid_cell_rect(/*index*/5, /*cols*/4, /*cell*/160, /*gap*/16,
                                     /*ox*/40, /*oy*/120);
    // index 5 -> row 1, col 1
    CHECK_EQ(c.x, 40.0f + 1 * (160.0f + 16.0f));
    CHECK_EQ(c.y, 120.0f + 1 * (160.0f + 16.0f));
    CHECK_EQ(c.w, 160.0f);
}

TEST(widgets_grid_hit)
{
    // 4 cells, cols 4, cell 160 gap 16, origin (40,120)
    int hit = ui::grid_hit(40 + 1 * 176 + 5, 120 + 5, /*count*/4, /*cols*/4,
                           160, 16, 40, 120);
    CHECK_EQ(hit, 1);
    CHECK_EQ(ui::grid_hit(0, 0, 4, 4, 160, 16, 40, 120), -1);     // miss
}

TEST(widgets_fit_rect_preserves_aspect)
{
    SDL_FRect box{0, 0, 100, 100};
    SDL_FRect f = ui::fit_rect(200, 100, box);   // 2:1 image into square box
    CHECK_EQ(f.w, 100.0f);
    CHECK_EQ(f.h, 50.0f);
    CHECK_EQ(f.x, 0.0f);
    CHECK_EQ(f.y, 25.0f);                          // vertically centred
}
```

> Note on `grid_columns(680,160,16)`: `floor((680+16)/(160+16)) = floor(696/176) = floor(3.95) = 3`. Fix the first CHECK to expect `3` after Step 2 (it is written wrong on purpose to force a fail-first; correct it to `CHECK_EQ(ui::grid_columns(680.0f,160.0f,16.0f), 3)`).

- [ ] **Step 2: Write the header**

`src/ui/widgets.h`:
```cpp
#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <string_view>

#include "gfx/color.h"

namespace gfx { class Renderer; class FontAtlas; }

namespace ui {

// --- Pure layout / hit-testing -------------------------------------------
[[nodiscard]] bool point_in_rect(float x, float y, const SDL_FRect& r) noexcept;

// How many `cell`-wide columns (separated by `gap`) fit in `avail_w` (min 1).
[[nodiscard]] int grid_columns(float avail_w, float cell, float gap) noexcept;

// Rect of cell `index` in a `cols`-wide grid with the given cell/gap/origin.
[[nodiscard]] SDL_FRect grid_cell_rect(int index, int cols, float cell, float gap,
                                       float origin_x, float origin_y) noexcept;

// Index of the cell under (mx,my), or -1. Considers indices [0,count).
[[nodiscard]] int grid_hit(float mx, float my, int count, int cols, float cell,
                           float gap, float origin_x, float origin_y) noexcept;

// Aspect-fit a `w`x`h` source centred inside `box`.
[[nodiscard]] SDL_FRect fit_rect(float w, float h, const SDL_FRect& box) noexcept;

// --- Thin draw helpers (not unit-tested) ---------------------------------
struct Button { SDL_FRect rect; std::string label; };
void draw_button(gfx::Renderer& r, gfx::FontAtlas& font, const Button& b,
                 bool hover, bool active);
void draw_text_field(gfx::Renderer& r, gfx::FontAtlas& font, const SDL_FRect& box,
                     std::string_view shown, bool focused);

} // namespace ui
```

- [ ] **Step 3: Write the implementation**

`src/ui/widgets.cpp`:
```cpp
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>

#include "gfx/renderer.h"
#include "gfx/text.h"

namespace ui {

bool point_in_rect(float x, float y, const SDL_FRect& r) noexcept
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

int grid_columns(float avail_w, float cell, float gap) noexcept
{
    if (cell <= 0.0f) return 1;
    int cols = static_cast<int>(std::floor((avail_w + gap) / (cell + gap)));
    return cols < 1 ? 1 : cols;
}

SDL_FRect grid_cell_rect(int index, int cols, float cell, float gap,
                         float origin_x, float origin_y) noexcept
{
    if (cols < 1) cols = 1;
    const int row = index / cols;
    const int col = index % cols;
    return SDL_FRect{origin_x + static_cast<float>(col) * (cell + gap),
                     origin_y + static_cast<float>(row) * (cell + gap),
                     cell, cell};
}

int grid_hit(float mx, float my, int count, int cols, float cell, float gap,
             float origin_x, float origin_y) noexcept
{
    for (int i = 0; i < count; ++i)
        if (point_in_rect(mx, my, grid_cell_rect(i, cols, cell, gap, origin_x, origin_y)))
            return i;
    return -1;
}

SDL_FRect fit_rect(float w, float h, const SDL_FRect& box) noexcept
{
    if (w <= 0.0f || h <= 0.0f) return box;
    const float scale = std::min(box.w / w, box.h / h);
    const float nw = w * scale, nh = h * scale;
    return SDL_FRect{box.x + (box.w - nw) * 0.5f, box.y + (box.h - nh) * 0.5f, nw, nh};
}

void draw_button(gfx::Renderer& r, gfx::FontAtlas& font, const Button& b,
                 bool hover, bool active)
{
    gfx::Color bg = active ? gfx::Color{90, 60, 150, 255}
                  : hover  ? gfx::Color{70, 70, 95, 255}
                           : gfx::Color{55, 55, 70, 255};
    r.draw_rect(b.rect, bg);
    r.draw_rect(b.rect, gfx::Color{120, 80, 200, 255}, /*filled*/ false);
    const int tw = font.measure(b.label);
    r.draw_text(font, b.rect.x + (b.rect.w - static_cast<float>(tw)) * 0.5f,
                b.rect.y + b.rect.h * 0.5f - 14.0f, b.label,
                gfx::Color{235, 235, 240, 255});
}

void draw_text_field(gfx::Renderer& r, gfx::FontAtlas& font, const SDL_FRect& box,
                     std::string_view shown, bool focused)
{
    r.draw_rect(box, gfx::Color{40, 40, 50, 255});
    r.draw_rect(box, focused ? gfx::Color{120, 80, 200, 255}
                             : gfx::Color{80, 80, 95, 255}, /*filled*/ false);
    r.draw_text(font, box.x + 10.0f, box.y + box.h * 0.5f - 14.0f, shown,
                gfx::Color{230, 230, 235, 255});
}

} // namespace ui
```

- [ ] **Step 4: Correct the intentional fail and run** — change the first CHECK in `widgets_grid_columns_and_cell` to expect `3`. Run `scripts/test.sh` → `widgets_*` PASS.
- [ ] **Step 5: Commit**

```bash
git add src/ui/widgets.h src/ui/widgets.cpp tests/ui/test_widgets.cpp
git commit -m "Phase 5: ui widgets (grid layout/hit-test + button/field draws)"
```

---

## Task 7: ui/screen.h — screen interface

**Files:**
- Create: `src/ui/screen.h`

No test (pure interface). It is header-only and consumed by the screens and `App`.

- [ ] **Step 1: Write the header**

`src/ui/screen.h`:
```cpp
#pragma once

#include <SDL3/SDL.h>

namespace gfx { class Renderer; }

namespace ui {

enum class NavKind { None, ToUnlock, ToGallery, Quit };

struct Nav { NavKind kind = NavKind::None; };

// One full-window screen. App owns exactly one active screen, forwards raw SDL
// events to it, and consumes its transition request each frame via take_nav().
class Screen {
public:
    virtual ~Screen() = default;

    virtual void on_enter() {}
    virtual void on_exit()  {}

    virtual void handle_event(const SDL_Event& e) = 0;
    virtual void update(double dt) { (void)dt; }
    virtual void render(gfx::Renderer& r) = 0;

    [[nodiscard]] Nav take_nav() noexcept { Nav n = nav_; nav_ = {}; return n; }

protected:
    void request(NavKind k) noexcept { nav_ = Nav{k}; }

private:
    Nav nav_{};
};

} // namespace ui
```

- [ ] **Step 2: Compile-check** — `scripts/build.sh` still builds `osv` (header is unused so far; this just confirms it parses). Expected: `Built: build/bin/Debug/osv`.
- [ ] **Step 3: Commit**

```bash
git add src/ui/screen.h
git commit -m "Phase 5: ui::Screen interface + Nav transition value"
```

---

## Task 8: platform/file_dialog — async open dialog

**Files:**
- Create: `src/platform/file_dialog.h`, `src/platform/file_dialog.cpp`

Not unit-tested (wraps the OS dialog). Verified by compiling and, later, running.

- [ ] **Step 1: Write the header**

`src/platform/file_dialog.h`:
```cpp
#pragma once

#include <SDL3/SDL.h>

#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace platform {

// Thin wrapper over SDL_ShowOpenFileDialog. The SDL callback may run on another
// thread, so results land in a mutex-guarded slot and are delivered to the main
// thread by polling take_result() once per frame.
class FileDialog {
public:
    void open_vault(SDL_Window* parent);    // *.osv (single)
    void open_images(SDL_Window* parent);    // common image types (multi)
    void open_keyfile(SDL_Window* parent);   // any file (single)

    [[nodiscard]] bool busy() const noexcept;

    // Non-nullopt exactly once after a dialog closes. Empty vector => cancelled.
    [[nodiscard]] std::optional<std::vector<std::string>> take_result();

private:
    enum class St { Idle, Open, Done };
    static void SDLCALL on_files(void* userdata, const char* const* filelist, int filter);

    mutable std::mutex       mtx_;
    St                       state_ = St::Idle;
    std::vector<std::string> paths_;
};

} // namespace platform
```

- [ ] **Step 2: Write the implementation**

`src/platform/file_dialog.cpp`:
```cpp
#include "platform/file_dialog.h"

#include <print>

namespace platform {

void SDLCALL FileDialog::on_files(void* userdata, const char* const* filelist, int)
{
    auto* self = static_cast<FileDialog*>(userdata);
    std::lock_guard lk(self->mtx_);
    self->paths_.clear();
    if (filelist) {
        for (const char* const* p = filelist; *p != nullptr; ++p)
            self->paths_.emplace_back(*p);
    } else {
        std::println(stderr, "[Platform] File dialog error: {}", SDL_GetError());
    }
    self->state_ = St::Done;
}

void FileDialog::open_vault(SDL_Window* parent)
{
    { std::lock_guard lk(mtx_); if (state_ == St::Open) return; state_ = St::Open; paths_.clear(); }
    static const SDL_DialogFileFilter f[] = {{"OSV Vault", "osv"}, {"All files", "*"}};
    SDL_ShowOpenFileDialog(on_files, this, parent, f, 2, nullptr, /*allow_many*/ false);
}

void FileDialog::open_images(SDL_Window* parent)
{
    { std::lock_guard lk(mtx_); if (state_ == St::Open) return; state_ = St::Open; paths_.clear(); }
    static const SDL_DialogFileFilter f[] = {{"Images", "jpg;jpeg;png;gif;bmp;tga;hdr"},
                                             {"All files", "*"}};
    SDL_ShowOpenFileDialog(on_files, this, parent, f, 2, nullptr, /*allow_many*/ true);
}

void FileDialog::open_keyfile(SDL_Window* parent)
{
    { std::lock_guard lk(mtx_); if (state_ == St::Open) return; state_ = St::Open; paths_.clear(); }
    static const SDL_DialogFileFilter f[] = {{"All files", "*"}};
    SDL_ShowOpenFileDialog(on_files, this, parent, f, 1, nullptr, /*allow_many*/ false);
}

bool FileDialog::busy() const noexcept
{
    std::lock_guard lk(mtx_);
    return state_ == St::Open;
}

std::optional<std::vector<std::string>> FileDialog::take_result()
{
    std::lock_guard lk(mtx_);
    if (state_ != St::Done) return std::nullopt;
    state_ = St::Idle;
    return std::move(paths_);
}

} // namespace platform
```

- [ ] **Step 3: Compile-check** — `scripts/build.sh` builds `osv`.
- [ ] **Step 4: Commit**

```bash
git add src/platform/file_dialog.h src/platform/file_dialog.cpp
git commit -m "Phase 5: platform::FileDialog async open-dialog wrapper"
```

---

## Task 9: ui/unlock_screen

**Files:**
- Create: `src/ui/unlock_screen.h`, `src/ui/unlock_screen.cpp`

Not unit-tested (SDL + crypto + I/O). The pure parts it relies on (`decide_submit`, `SecureTextField`, `point_in_rect`) are already covered.

- [ ] **Step 1: Write the header**

`src/ui/unlock_screen.h`:
```cpp
#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>

#include "ui/screen.h"
#include "ui/secure_text_field.h"

namespace gfx { class Window; class FontAtlas; class Renderer; }
namespace vault { class Vault; }
namespace platform { class FileDialog; }

namespace ui {

class UnlockScreen : public Screen {
public:
    UnlockScreen(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                 platform::FileDialog& dlg, std::filesystem::path vault_path);

    void on_enter() override;
    void on_exit() override;
    void handle_event(const SDL_Event& e) override;
    void update(double dt) override;
    void render(gfx::Renderer& r) override;

private:
    struct Layout { SDL_FRect keyfile_btn, other_btn, mode_btn, submit_btn; };
    [[nodiscard]] Layout layout() const;
    void submit();

    enum class Pending { None, Vault, Keyfile };

    gfx::Window&          win_;
    gfx::FontAtlas&       font_;
    vault::Vault&         vault_;
    platform::FileDialog& dlg_;
    std::filesystem::path vault_path_;
    bool                  create_mode_;
    int                   focus_ = 0;   // 0 = password, 1 = confirm
    SecureTextField       pw_;
    SecureTextField       confirm_;
    std::string           keyfile_path_;
    std::string           error_;
    Pending               pending_ = Pending::None;
};

} // namespace ui
```

- [ ] **Step 2: Write the implementation**

`src/ui/unlock_screen.cpp`:
```cpp
#include "ui/unlock_screen.h"

#include <monocypher.h>

#include <vector>

#include "crypto/kdf.h"
#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "platform/paths.h"
#include "ui/unlock_logic.h"
#include "ui/widgets.h"
#include "vault/vault.h"

namespace ui {

UnlockScreen::UnlockScreen(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                           platform::FileDialog& dlg, std::filesystem::path vault_path)
    : win_(win), font_(font), vault_(vault), dlg_(dlg),
      vault_path_(std::move(vault_path)),
      create_mode_(!std::filesystem::exists(vault_path_))
{
}

void UnlockScreen::on_enter() { SDL_StartTextInput(win_.sdl_window()); }

void UnlockScreen::on_exit()
{
    SDL_StopTextInput(win_.sdl_window());
    pw_.clear();
    confirm_.clear();
}

UnlockScreen::Layout UnlockScreen::layout() const
{
    const float W = static_cast<float>(win_.width());
    const float H = static_cast<float>(win_.height());
    const float bw = 200.0f, bh = 44.0f, gap = 16.0f, row = H - 140.0f;
    return Layout{
        .keyfile_btn = {60.0f,                  row, bw, bh},
        .other_btn   = {60.0f + (bw + gap),     row, bw, bh},
        .mode_btn    = {60.0f + 2 * (bw + gap), row, bw, bh},
        .submit_btn  = {W - 60.0f - bw,         row, bw, bh},
    };
}

void UnlockScreen::handle_event(const SDL_Event& e)
{
    switch (e.type) {
        case SDL_EVENT_TEXT_INPUT: {
            SecureTextField& f = (create_mode_ && focus_ == 1) ? confirm_ : pw_;
            f.push_utf8(e.text.text);
            break;
        }
        case SDL_EVENT_KEY_DOWN: {
            SecureTextField& f = (create_mode_ && focus_ == 1) ? confirm_ : pw_;
            switch (e.key.key) {
                case SDLK_BACKSPACE: f.backspace(); break;
                case SDLK_TAB:       if (create_mode_) focus_ ^= 1; break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:  submit(); break;
                default: break;
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            const Layout L = layout();
            const float x = e.button.x, y = e.button.y;
            if (point_in_rect(x, y, L.mode_btn)) {
                create_mode_ = !create_mode_; focus_ = 0; error_.clear();
            } else if (point_in_rect(x, y, L.keyfile_btn)) {
                pending_ = Pending::Keyfile; dlg_.open_keyfile(win_.sdl_window());
            } else if (point_in_rect(x, y, L.other_btn)) {
                pending_ = Pending::Vault;   dlg_.open_vault(win_.sdl_window());
            } else if (point_in_rect(x, y, L.submit_btn)) {
                submit();
            }
            break;
        }
        default: break;
    }
}

void UnlockScreen::update(double)
{
    if (auto res = dlg_.take_result()) {
        if (!res->empty()) {
            if (pending_ == Pending::Vault) {
                vault_path_  = (*res)[0];
                create_mode_ = !std::filesystem::exists(vault_path_);
            } else if (pending_ == Pending::Keyfile) {
                keyfile_path_ = (*res)[0];
            }
        }
        pending_ = Pending::None;
    }
}

void UnlockScreen::submit()
{
    using enum vault::VaultResult;
    error_.clear();

    std::vector<uint8_t> keyfile;
    if (!keyfile_path_.empty()) {
        auto kf = platform::read_file(keyfile_path_);
        if (!kf) { error_ = "Cannot read keyfile."; return; }
        keyfile = std::move(*kf);
    }

    const SubmitDecision d = decide_submit(create_mode_, pw_.bytes(), confirm_.bytes());
    if (d.error) {
        error_ = d.error;
        if (!keyfile.empty()) crypto_wipe(keyfile.data(), keyfile.size());
        return;
    }

    vault::VaultResult r;
    if (d.action == SubmitAction::Create) {
        r = vault::Vault::create(vault_path_.string(), pw_.bytes(), keyfile,
                                 crypto::DEFAULT_KDF_PARAMS, vault_);
    } else {
        r = vault::Vault::open(vault_path_.string(), vault_);
        if (r == Ok) r = vault_.unlock(pw_.bytes(), keyfile);
    }
    if (!keyfile.empty()) crypto_wipe(keyfile.data(), keyfile.size());

    if (r == Ok) {
        pw_.clear();
        confirm_.clear();
        request(NavKind::ToGallery);
        return;
    }
    switch (r) {
        case AuthFailed: error_ = "Wrong password or keyfile."; break;
        case BadFormat:  error_ = "Not a valid vault file.";    break;
        case IoError:    error_ = "Could not read/write the vault file."; break;
        default:         error_ = "Unlock failed.";             break;
    }
}

void UnlockScreen::render(gfx::Renderer& r)
{
    const float W = static_cast<float>(win_.width());
    const float H = static_cast<float>(win_.height());

    r.draw_text(font_, 60, 50, create_mode_ ? "Create Vault" : "Unlock Vault",
                gfx::Color{240, 240, 245, 255});
    r.draw_text(font_, 60, 100, "Vault: " + vault_path_.string(),
                gfx::Color{150, 150, 160, 255});

    const float fx = 60, fw = W - 120, fh = 44;
    r.draw_text(font_, fx, 134, "Password", gfx::Color{150, 150, 160, 255});
    draw_text_field(r, font_, {fx, 160, fw, fh},
                    std::string(pw_.length(), '*'), !create_mode_ || focus_ == 0);
    if (create_mode_) {
        r.draw_text(font_, fx, 234, "Confirm", gfx::Color{150, 150, 160, 255});
        draw_text_field(r, font_, {fx, 260, fw, fh},
                        std::string(confirm_.length(), '*'), focus_ == 1);
    }

    const Layout L = layout();
    draw_button(r, font_, {L.keyfile_btn,
                keyfile_path_.empty() ? "Keyfile: none" : "Keyfile: set"}, false, false);
    draw_button(r, font_, {L.other_btn, "Open other..."}, false, false);
    draw_button(r, font_, {L.mode_btn,
                create_mode_ ? "Have a vault?" : "New vault?"}, false, false);
    draw_button(r, font_, {L.submit_btn, create_mode_ ? "Create" : "Unlock"}, false, false);

    if (!error_.empty())
        r.draw_text(font_, 60, H - 70, error_, gfx::Color{230, 120, 120, 255});
}

} // namespace ui
```

- [ ] **Step 3: Compile-check** — `scripts/build.sh` builds `osv`.
- [ ] **Step 4: Commit**

```bash
git add src/ui/unlock_screen.h src/ui/unlock_screen.cpp
git commit -m "Phase 5: ui::UnlockScreen (unlock/create + keyfile + dialog)"
```

---

## Task 10: ui/gallery_grid

**Files:**
- Create: `src/ui/gallery_grid.h`, `src/ui/gallery_grid.cpp`

- [ ] **Step 1: Write the header**

`src/ui/gallery_grid.h`:
```cpp
#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "ui/nav_model.h"
#include "ui/screen.h"

namespace gfx { class Window; class FontAtlas; class Renderer; class TextureCache; }
namespace vault { class Vault; struct IndexNode; }
namespace platform { class FileDialog; }

namespace ui {

class GalleryGrid : public Screen {
public:
    GalleryGrid(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                gfx::TextureCache& cache, platform::FileDialog& dlg);

    void on_enter() override;
    void handle_event(const SDL_Event& e) override;
    void update(double dt) override;
    void render(gfx::Renderer& r) override;

private:
    void refresh();
    void open_selected();
    void go_up();
    void start_import();
    void start_naming();
    void finish_naming();
    void do_import(const std::string& file_path);
    [[nodiscard]] bool current_allows_images() const;
    [[nodiscard]] bool current_allows_galleries() const;
    SDL_Texture*       thumb_texture(const vault::IndexNode& node);

    gfx::Window&          win_;
    gfx::FontAtlas&       font_;
    vault::Vault&         vault_;
    gfx::TextureCache&    cache_;
    platform::FileDialog& dlg_;
    NavModel              nav_;
    std::vector<const vault::IndexNode*> children_;
    int                   cols_ = 1;
    std::string           error_;
    bool                  naming_ = false;
    std::string           name_buf_;
};

} // namespace ui
```

- [ ] **Step 2: Write the implementation**

`src/ui/gallery_grid.cpp`:
```cpp
#include "ui/gallery_grid.h"

#include <filesystem>

#include "crypto/secure_mem.h"
#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/texture_cache.h"
#include "gfx/window.h"
#include "image/decode.h"
#include "platform/file_dialog.h"
#include "platform/paths.h"
#include "ui/input.h"
#include "ui/widgets.h"
#include "vault/index.h"
#include "vault/vault.h"

namespace ui {

namespace {
constexpr float OX = 40, OY = 160, CELL = 160, GAP = 16;
}

GalleryGrid::GalleryGrid(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                         gfx::TextureCache& cache, platform::FileDialog& dlg)
    : win_(win), font_(font), vault_(vault), cache_(cache), dlg_(dlg)
{
}

void GalleryGrid::on_enter() { refresh(); }

void GalleryGrid::refresh()
{
    children_ = vault_.list(nav_.path());
    nav_.set_count(static_cast<int>(children_.size()));
}

bool GalleryGrid::current_allows_images() const
{
    for (const auto* c : children_) if (c->is_gallery()) return false;
    return true;
}

bool GalleryGrid::current_allows_galleries() const
{
    for (const auto* c : children_) if (c->is_image()) return false;
    return true;
}

void GalleryGrid::open_selected()
{
    const int s = nav_.selected();
    if (s < 0 || s >= static_cast<int>(children_.size())) return;
    const vault::IndexNode* n = children_[s];
    if (n->is_gallery()) { nav_.enter(n->name); refresh(); }
    // Image selection opens the viewer in Phase 6; no-op here.
}

void GalleryGrid::go_up()
{
    if (!nav_.up()) { vault_.lock(); request(NavKind::ToUnlock); return; }
    refresh();
}

void GalleryGrid::start_import()
{
    if (!current_allows_images()) {
        error_ = "Can't import here: this gallery holds sub-galleries.";
        return;
    }
    error_.clear();
    dlg_.open_images(win_.sdl_window());
}

void GalleryGrid::do_import(const std::string& file_path)
{
    using enum vault::VaultResult;
    auto bytes = platform::read_file(file_path);
    if (!bytes) { error_ = "Could not read " + file_path; return; }

    const std::string fname = std::filesystem::path(file_path).filename().string();
    switch (vault_.add_image(nav_.path(), *bytes, fname)) {
        case Ok:            break;
        case AlreadyExists: error_ = "Already exists: " + fname; break;
        case InvalidArg:    error_ = "Cannot add an image here."; break;
        default:            error_ = "Import failed: " + fname; break;
    }
}

void GalleryGrid::start_naming()
{
    if (!current_allows_galleries()) {
        error_ = "Can't create a sub-gallery in an image gallery.";
        return;
    }
    naming_ = true;
    name_buf_.clear();
    error_.clear();
    SDL_StartTextInput(win_.sdl_window());
}

void GalleryGrid::finish_naming()
{
    using enum vault::VaultResult;
    naming_ = false;
    SDL_StopTextInput(win_.sdl_window());
    if (name_buf_.empty()) return;

    const std::string base = nav_.path();
    const std::string full = base.empty() ? name_buf_ : base + "/" + name_buf_;
    switch (vault_.create_gallery(full)) {
        case Ok:            break;
        case AlreadyExists: error_ = "Gallery already exists."; break;
        case InvalidArg:    error_ = "Invalid gallery name/location."; break;
        default:            error_ = "Could not create gallery."; break;
    }
    name_buf_.clear();
    refresh();
}

SDL_Texture* GalleryGrid::thumb_texture(const vault::IndexNode& node)
{
    if (node.meta.thumb_length == 0) return nullptr;
    const uint64_t key = node.meta.data_offset;
    if (SDL_Texture* t = cache_.get(key)) return t;

    crypto::SecureBytes sb;
    if (vault_.read_thumbnail(node, sb) != vault::VaultResult::Ok) return nullptr;
    auto img = image::decode_from_memory(sb.as_span());
    if (!img) return nullptr;
    return cache_.get_or_upload(key, *img);
}

void GalleryGrid::handle_event(const SDL_Event& e)
{
    if (naming_) {
        switch (e.type) {
            case SDL_EVENT_TEXT_INPUT: name_buf_ += e.text.text; break;
            case SDL_EVENT_KEY_DOWN:
                if (e.key.key == SDLK_BACKSPACE && !name_buf_.empty())
                    name_buf_.pop_back();
                else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)
                    finish_naming();
                else if (e.key.key == SDLK_ESCAPE) {
                    naming_ = false; name_buf_.clear();
                    SDL_StopTextInput(win_.sdl_window());
                }
                break;
            default: break;
        }
        return;
    }

    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
            switch (map_key(e.key.key, e.key.mod)) {
                case InputAction::NavLeft:    nav_.move(-1);     break;
                case InputAction::NavRight:   nav_.move(1);      break;
                case InputAction::NavUp:      nav_.move(-cols_); break;
                case InputAction::NavDown:    nav_.move(cols_);  break;
                case InputAction::Select:     open_selected();   break;
                case InputAction::Back:       go_up();           break;
                case InputAction::Import:     start_import();    break;
                case InputAction::NewGallery: start_naming();    break;
                default: break;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            const int idx = grid_hit(e.button.x, e.button.y,
                                     static_cast<int>(children_.size()),
                                     cols_, CELL, GAP, OX, OY);
            if (idx >= 0) {
                nav_.select(idx);
                if (children_[idx]->is_gallery()) open_selected();
            }
            break;
        }
        default: break;
    }
}

void GalleryGrid::update(double)
{
    if (auto res = dlg_.take_result()) {
        if (!res->empty()) {
            for (const auto& p : *res) do_import(p);
            refresh();
        }
    }
}

void GalleryGrid::render(gfx::Renderer& r)
{
    const float W = static_cast<float>(win_.width());
    const float H = static_cast<float>(win_.height());
    cols_ = grid_columns(W - 2 * OX, CELL, GAP);

    std::string crumb = "/";
    for (const auto& s : nav_.segments()) { crumb += s; crumb += '/'; }
    r.draw_text(font_, OX, 40, crumb, gfx::Color{200, 200, 210, 255});
    r.draw_text(font_, OX, 90, "[I] Import   [N] New Gallery   [Enter] Open   [Esc] Up",
                gfx::Color{120, 120, 130, 255});

    for (size_t i = 0; i < children_.size(); ++i) {
        const SDL_FRect cellr = grid_cell_rect(static_cast<int>(i), cols_, CELL, GAP, OX, OY);
        const vault::IndexNode* n = children_[i];
        const bool sel = (static_cast<int>(i) == nav_.selected());
        r.draw_rect(cellr, sel ? gfx::Color{70, 70, 90, 255} : gfx::Color{45, 45, 55, 255});

        if (n->is_gallery()) {
            r.draw_rect({cellr.x + 30, cellr.y + 40, CELL - 60, CELL - 90},
                        gfx::Color{200, 170, 90, 255});
        } else if (SDL_Texture* tex = thumb_texture(*n)) {
            float tw = 0, th = 0;
            SDL_GetTextureSize(tex, &tw, &th);
            r.draw_image(tex, fit_rect(tw, th, {cellr.x + 6, cellr.y + 6,
                                                CELL - 12, CELL - 40}));
        } else {
            r.draw_text(font_, cellr.x + 10, cellr.y + CELL / 2 - 14, "(no thumb)",
                        gfx::Color{150, 150, 160, 255});
        }
        r.draw_text(font_, cellr.x + 8, cellr.y + CELL - 30, n->name,
                    gfx::Color{230, 230, 235, 255});
        if (sel) r.draw_rect(cellr, gfx::Color{120, 80, 200, 255}, /*filled*/ false);
    }

    if (!error_.empty())
        r.draw_text(font_, OX, H - 36, error_, gfx::Color{230, 120, 120, 255});

    if (naming_) {
        const float mw = W * 0.6f, mh = 120, mx = (W - mw) / 2, my = (H - mh) / 2;
        r.draw_rect({mx, my, mw, mh}, gfx::Color{30, 30, 40, 255});
        r.draw_rect({mx, my, mw, mh}, gfx::Color{120, 80, 200, 255}, /*filled*/ false);
        r.draw_text(font_, mx + 16, my + 16, "New gallery name:",
                    gfx::Color{220, 220, 225, 255});
        draw_text_field(r, font_, {mx + 16, my + 56, mw - 32, 44}, name_buf_, true);
    }
}

} // namespace ui
```

- [ ] **Step 3: Compile-check** — `scripts/build.sh` builds `osv`.
- [ ] **Step 4: Commit**

```bash
git add src/ui/gallery_grid.h src/ui/gallery_grid.cpp
git commit -m "Phase 5: ui::GalleryGrid (breadcrumb grid, import, new gallery)"
```

---

## Task 11: Window poll_event + App screen state machine

**Files:**
- Modify: `src/gfx/window.h`, `src/gfx/window.cpp`
- Modify: `src/app/app.h`, `src/app/app.cpp`

- [ ] **Step 1: Window — replace process_events with poll_event**

In `src/gfx/window.h`, remove the `process_events` declaration and its doc comment, and add (next to `begin_frame`):
```cpp
    /// Poll one pending event into `out`. Returns false when the queue is empty.
    /// (App decides what quit/close means — the window no longer self-quits.)
    [[nodiscard]] bool poll_event(SDL_Event& out);
```

In `src/gfx/window.cpp`, delete the entire `Window::process_events` function body and replace it with:
```cpp
bool Window::poll_event(SDL_Event& out)
{
    return SDL_PollEvent(&out);
}
```

- [ ] **Step 2: App header**

Replace `src/app/app.h` with:
```cpp
#pragma once

#include <memory>

#include "gfx/text.h"
#include "gfx/texture_cache.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "ui/screen.h"
#include "vault/vault.h"

namespace app {

enum class State { Locked, Browsing }; // Viewing reserved for Phase 6

class App {
public:
    App()  = default;
    ~App() = default;

    App(const App&)            = delete;
    App& operator=(const App&) = delete;

    [[nodiscard]] bool init();
    void run();
    void shutdown();

private:
    void to_unlock();
    void to_gallery();

    gfx::Window                        window_;
    gfx::FontAtlas                     font_;
    bool                               font_ready_ = false;
    std::unique_ptr<gfx::TextureCache> cache_;
    platform::FileDialog               dialog_;
    vault::Vault                       vault_;
    std::unique_ptr<ui::Screen>        screen_;
    State                              state_ = State::Locked;
};

} // namespace app
```

- [ ] **Step 3: App implementation**

Replace `src/app/app.cpp` with:
```cpp
#include "app.h"

#include <print>

#include "gfx/renderer.h"
#include "platform/paths.h"
#include "ui/gallery_grid.h"
#include "ui/unlock_screen.h"

#ifndef OSV_DEFAULT_FONT
#define OSV_DEFAULT_FONT "assets/fonts/NotoSans-Regular.ttf"
#endif

namespace app {

bool App::init()
{
    if (!window_.init()) {
        std::println(stderr, "[App] Window initialisation failed.");
        return false;
    }

    font_ready_ = font_.bake_from_file(OSV_DEFAULT_FONT, 28.0f);
    if (!font_ready_)
        std::println(stderr, "[App] Font atlas unavailable ('{}').", OSV_DEFAULT_FONT);

    cache_ = std::make_unique<gfx::TextureCache>(window_.sdl_renderer());
    to_unlock();

    std::println("[App] Initialised (Phase 5 — UI layer).");
    return true;
}

void App::to_unlock()
{
    state_  = State::Locked;
    screen_ = std::make_unique<ui::UnlockScreen>(window_, font_, vault_, dialog_,
                                                 platform::default_vault_path());
    screen_->on_enter();
}

void App::to_gallery()
{
    state_  = State::Browsing;
    screen_ = std::make_unique<ui::GalleryGrid>(window_, font_, vault_, *cache_, dialog_);
    screen_->on_enter();
}

void App::run()
{
    bool     running = true;
    uint64_t prev    = SDL_GetTicks();

    while (running) {
        SDL_Event e;
        while (window_.poll_event(e)) {
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                running = false;
            else if (screen_)
                screen_->handle_event(e);
        }

        const uint64_t now = SDL_GetTicks();
        const double   dt  = static_cast<double>(now - prev) / 1000.0;
        prev = now;

        if (screen_) screen_->update(dt);

        window_.begin_frame(18, 18, 24);
        if (screen_) {
            gfx::Renderer r(window_.sdl_renderer());
            screen_->render(r);
        }
        window_.end_frame();

        if (screen_) {
            switch (screen_->take_nav().kind) {
                case ui::NavKind::ToGallery: screen_->on_exit(); to_gallery(); break;
                case ui::NavKind::ToUnlock:  screen_->on_exit(); to_unlock();  break;
                case ui::NavKind::Quit:      running = false; break;
                case ui::NavKind::None:      break;
            }
        }
    }
}

void App::shutdown()
{
    if (screen_) { screen_->on_exit(); screen_.reset(); }
    vault_.lock();                 // wipe master key
    if (cache_) cache_->clear();   // destroy thumbnail textures before the renderer
    font_.release_texture();
    window_.shutdown();
    std::println("[App] Clean shutdown.");
}

} // namespace app
```

- [ ] **Step 4: Build the app and run the full test suite**

Run: `scripts/gen.sh && scripts/build.sh`
Expected: `Built: build/bin/Debug/osv`.

Run: `scripts/test.sh`
Expected: all tests PASS (the new units + all pre-existing Phase 0–4 tests).

- [ ] **Step 5: Commit**

```bash
git add src/gfx/window.h src/gfx/window.cpp src/app/app.h src/app/app.cpp
git commit -m "Phase 5: window poll_event + App screen state machine"
```

---

## Task 12: Manual end-to-end verification

No code. Confirms the SDL-driven flow that unit tests cannot cover.

- [ ] **Step 1: Launch** — `build/bin/Debug/osv`. A window titled "Obscura-Safe-Vault" opens on the **Create Vault** screen (no vault exists yet).
- [ ] **Step 2: Create** — type a password, Tab to confirm, type it again, click **Create**. The screen switches to the (empty) gallery grid with breadcrumb `/`.
- [ ] **Step 3: New gallery** — press `N`, type `photos`, Enter. A `photos` folder tile appears.
- [ ] **Step 4: Import** — press Enter on `photos` to descend, press `I`, pick one or more image files. Thumbnails appear in the grid. (Importing into `photos` while it holds galleries is blocked — verify by trying `I` at root after creating a gallery: it shows the "holds sub-galleries" error.)
- [ ] **Step 5: Navigate** — arrow keys move the selection highlight; `Esc`/Backspace goes up; `Esc` at root returns to the unlock screen.
- [ ] **Step 6: Re-unlock** — now on the **Unlock Vault** screen (vault now exists). Enter the wrong password → "Wrong password or keyfile."; enter the right one → the `photos` gallery and its thumbnails are still there (persistence + decryption round-trip).
- [ ] **Step 7: Close** — close the window; terminal shows `[App] Clean shutdown.` with no ASan complaints if built with `--asan`.
- [ ] **Step 8: Commit** (if you kept a short verification note; otherwise skip). No code change required.

---

## Task 13: Remove the design spec (project-owner cleanup request)

The owner asked that the brainstorming spec be removed once Phase 5 is complete.

- [ ] **Step 1: Remove the spec file**

```bash
git rm docs/superpowers/specs/2026-06-10-phase-5-ui-layer-design.md
```

- [ ] **Step 2: Commit**

```bash
git commit -m "Phase 5: remove design spec (superseded by implementation)"
```

(The implementation plan in `docs/superpowers/plans/` is kept as the durable record.)

---

## Self-Review

**Spec coverage:**
- Screen state machine / `App` ownership → Tasks 7, 11. ✓
- Window `poll_event` (no Esc-quit) → Task 11. ✓
- `platform/paths` (config dir, default vault) → Task 1. ✓
- `platform/file_dialog` (async, thread-safe) → Task 8. ✓
- `ui/input` `map_key` → Task 2. ✓
- `ui/widgets` layout/hit-test + draws → Task 6. ✓
- `SecureTextField` (mlock'd password) → Task 4. ✓
- Unlock/create flow + `decide_submit` + keyfile → Tasks 5, 9. ✓
- Gallery grid: breadcrumb, tiles, thumbnails, import, new gallery, leaf-invariant guards, Esc-at-root locks → Task 10. ✓
- Thumbnail cache keyed by `data_offset` → Task 10 (`thumb_texture`). ✓
- Security: vault locked + textures cleared on shutdown, no key/plaintext logging, keyfile wiped → Tasks 9, 11. ✓
- Tests for all pure units → Tasks 1–6. ✓
- premake test-target wiring → Task 1 Step 4. ✓

**Placeholder scan:** No TBD/TODO/"add error handling"-style gaps; every code step has complete code. The one intentional wrong value (`grid_columns` expecting 4) is explicitly flagged and corrected in Task 6 Steps 1/4 to drive a real fail-first.

**Type consistency:** `Nav`/`NavKind`/`request()`/`take_nav()` consistent across `screen.h`, screens, and `App`. `SubmitAction`/`SubmitDecision` consistent (Tasks 5, 9). `SecureTextField` API (`push_utf8`/`backspace`/`clear`/`length`/`bytes`) consistent (Tasks 4, 9). `NavModel` API (`enter`/`up`/`path`/`segments`/`set_count`/`move`/`select`/`selected`) consistent (Tasks 3, 10). Widget signatures (`grid_columns`/`grid_cell_rect`/`grid_hit`/`fit_rect`/`point_in_rect`/`draw_button`/`draw_text_field`) consistent (Tasks 6, 9, 10). Vault calls (`create`/`open`/`unlock`/`list`/`add_image`/`create_gallery`/`read_thumbnail`/`lock`) match `src/vault/vault.h`.
