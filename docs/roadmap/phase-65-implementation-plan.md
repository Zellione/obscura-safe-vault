# Phase 65 — Blocking vault migration: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the two lazy in-browse vault repairs with a one-time blocking, parallel, batch-committed migration offered at unlock, so a `.osv` stops mutating on the filesystem while the user is only reading.

**Architecture:** A watermark in `vault::VaultSettings` records what content backfills have run. A pure, zero-I/O tree walk (`vault::scan_migration`) reports pending work. `ui::MigrationJob` owns the vault exclusively behind a modal: one coordinator thread mutates the tree and appends chunks, a decode pool does the CPU work (decrypt → probe/sniff → encode poster) over copied chunk spans read through the any-thread-safe `vault::read_thumb_span`. One `commit_index()` at the end, then a conditional `compact()`. The lazy repair modules are deleted.

**Tech Stack:** C++23, premake5 → Ninja, Monocypher, FFmpeg (`OSV_VENDORED_AV`), stb_image, SDL3. Tests use the in-repo `tests/test_framework.h` (`TEST` / `CHECK` / `CHECK_EQ` / `REQUIRE`).

## Global Constraints

- **Spec:** `docs/roadmap/phase-65-blocking-migration.md`. Every decision there is binding.
- **Security invariants (CLAUDE.md):** no decrypted bytes to disk; `crypto_wipe` all key material; fresh 24-byte nonce per encrypt; authenticate before decrypt; never log keys/passwords/content; a vault file is untrusted input.
- **Phase 50 write protocol:** every append to `fp_` holds `write_mutex_`. Worker threads read **only** through `vault::read_thumb_span` (which uses `thumb_fp_` + `thumb_mutex_`); they must never touch `read_fp_` or the index tree.
- **Persisted-byte rule:** an out-of-range persisted value is **rejected on deserialise, not clamped** (the Phase 37/47/49 rule).
- **Layering:** `src/vault/` must not depend on `src/media/`. `vault.h` never includes `media/*.h`.
- **TDD:** every task writes the failing test first, watches it fail, then implements. **Two sanctioned exceptions, agreed with the owner before execution — these are not violations:**
  - **Task 6 (decode pool)** is a behaviour-preserving refactor. Its new test is expected to PASS serially before the pool exists; it is a regression net, not a red-green cycle. The real gates are the unchanged Task 4/5 tests plus ThreadSanitizer.
  - **Task 8 (UI wiring)** has no automated tests. This codebase has no headless harness for screens, and inventing one is out of scope for this phase. It is gated by manual verification through the `running-the-app` skill.
- **Tests run the whole suite** — `tests/test_framework.h` has no name filter. To check one test, grep the output.
- **`scripts/gen.sh`** must be re-run whenever a source file is added, moved, or removed (regenerates `compile_commands.json` for clangd).
- **⛔ Never merge.** Owner merges. Post the PR and stop.

---

### Task 1: Watermark fields in `VaultSettings` (`INDEX_VERSION` → 10)

**Files:**
- Modify: `src/vault/index.h` (struct at :250, version constant at :274)
- Modify: `src/vault/index.cpp` (`write_settings` at :309, `read_settings` at :411)
- Test: `tests/vault/test_migration.cpp` (create)

**Interfaces:**
- Consumes: nothing (first task).
- Produces: `VaultSettings::migrated_index_version` (`uint8_t`), `VaultSettings::migrated_probe_caps` (`uint16_t`), `vault::MIGRATION_INDEX_VERSION` (`uint8_t`, value 7), `vault::INDEX_VERSION` == 10.

- [ ] **Step 1: Write the failing test**

Create `tests/vault/test_migration.cpp`:

```cpp
#include "test_framework.h"

#include <vector>

#include "vault/index.h"

TEST(migration_watermark_round_trips_at_v10)
{
    vault::IndexNode root;
    root.type = vault::IndexNode::Type::Gallery;

    vault::VaultSettings s = vault::VaultSettings::seeded();
    s.migrated_index_version = 7;
    s.migrated_probe_caps    = 3;

    std::vector<uint8_t> blob;
    vault::serialize_index(root, {}, s, blob);
    CHECK(!blob.empty());
    CHECK_EQ(blob[0], vault::INDEX_VERSION);
    CHECK_EQ(vault::INDEX_VERSION, 10);

    vault::IndexNode out;
    std::vector<vault::SavedSearch> searches;
    vault::VaultSettings got;
    REQUIRE(vault::deserialize_index(blob, out, searches, got));
    CHECK_EQ(got.migrated_index_version, 7);
    CHECK_EQ(got.migrated_probe_caps, 3);
}

TEST(migration_watermark_defaults_to_zero_when_unset)
{
    vault::IndexNode root;
    root.type = vault::IndexNode::Type::Gallery;

    std::vector<uint8_t> blob;
    vault::serialize_index(root, {}, vault::VaultSettings::seeded(), blob);

    vault::IndexNode out;
    std::vector<vault::SavedSearch> searches;
    vault::VaultSettings got;
    REQUIRE(vault::deserialize_index(blob, out, searches, got));
    CHECK_EQ(got.migrated_index_version, 0);
    CHECK_EQ(got.migrated_probe_caps, 0);
}

TEST(migration_watermark_rejects_future_version)
{
    // A blob claiming migration to an index version this build does not know
    // is malformed input, not something to clamp.
    vault::IndexNode root;
    root.type = vault::IndexNode::Type::Gallery;

    vault::VaultSettings s = vault::VaultSettings::seeded();
    std::vector<uint8_t> blob;
    vault::serialize_index(root, {}, s, blob);

    // The watermark is the last 3 bytes of the settings block, which is the
    // tail of the blob: [.. migrated_index_version u8][migrated_probe_caps u16].
    blob[blob.size() - 3] = static_cast<uint8_t>(vault::INDEX_VERSION + 1);

    vault::IndexNode out;
    std::vector<vault::SavedSearch> searches;
    vault::VaultSettings got;
    CHECK(!vault::deserialize_index(blob, out, searches, got));
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
scripts/test.sh 2>&1 | grep -E "migration_watermark|error:" | head -20
```
Expected: a compile error — `'migrated_index_version' is not a member of 'vault::VaultSettings'`.

- [ ] **Step 3: Add the fields and the constant**

In `src/vault/index.h`, inside `struct VaultSettings` (after `tag_descriptions`):

```cpp
    // Phase 65: what content backfills have been run against this vault.
    // 0/0 is the correct reading for every pre-v10 blob — such a vault has
    // never been migrated. See docs/roadmap/phase-65-blocking-migration.md.
    uint8_t  migrated_index_version = 0;
    uint16_t migrated_probe_caps    = 0;
```

Below the `INDEX_VERSION` block, add:

```cpp
// Phase 65: the index version whose content backfills the migration performs.
// Bump ONLY when a NEW index version adds a field needing a content backfill —
// NOT on every INDEX_VERSION bump, or unrelated format changes would re-offer a
// migration that has nothing to do with them. Currently 7: the version that
// introduced ImageMeta::animated.
inline constexpr uint8_t MIGRATION_INDEX_VERSION = 7;
```

Update the version comment block and the constant:

```cpp
// v10: vault-global migration watermark appended to the settings block
// (Phase 65: migrated_index_version u8, migrated_probe_caps u16); pre-v10
// blobs read 0/0 and are therefore treated as never migrated.
inline constexpr uint8_t INDEX_VERSION = 10;
```

- [ ] **Step 4: Serialize the fields**

At the end of `write_settings` in `src/vault/index.cpp`:

```cpp
    // Phase 65 watermark. Clamped on write so a pathological in-memory value
    // can never emit a blob this reader would reject (the write_settings rule).
    w.u8(s.migrated_index_version > INDEX_VERSION ? 0 : s.migrated_index_version);
    w.u16(s.migrated_probe_caps);
```

- [ ] **Step 5: Deserialize the fields**

At the end of `read_settings` in `src/vault/index.cpp`, replacing the final `return true;` after `read_descriptions`:

```cpp
    if (!read_descriptions(r, s.tag_descriptions)) return false;

    // The watermark sub-block exists only from v10 on; a v9 blob ends after the
    // descriptions and must not be read past.
    if (version < 10) return true;

    const uint8_t migrated = r.u8();
    // Rejected, not clamped: a blob claiming a backfill from a future build is
    // malformed input, and silently accepting it would suppress a migration
    // this build genuinely still needs to run.
    if (!r.ok() || migrated > INDEX_VERSION) return false;
    s.migrated_index_version = migrated;

    s.migrated_probe_caps = r.u16();
    if (!r.ok()) return false;

    return true;
```

- [ ] **Step 6: Run the tests to verify they pass**

```bash
scripts/test.sh 2>&1 | tail -20
```
Expected: all tests pass, including the three `migration_watermark_*` tests.

- [ ] **Step 7: Verify the existing fuzz suite still passes**

`tests/vault/test_fuzz.cpp` mutates index blobs and asserts `deserialize_index` never crashes. The new tail bytes are now part of that surface.

```bash
scripts/test.sh --asan 2>&1 | tail -20
```
Expected: pass, no ASAN findings.

- [ ] **Step 8: Commit**

```bash
git add src/vault/index.h src/vault/index.cpp tests/vault/test_migration.cpp
git commit -m "feat(vault): migration watermark in VaultSettings (INDEX_VERSION 10)"
```

---

### Task 2: Detection module `vault/migration.*`

**Files:**
- Create: `src/vault/migration.h`, `src/vault/migration.cpp`
- Modify: `src/media/video_probe.h` (add `PROBE_CAPS_GEN`)
- Test: `tests/vault/test_migration.cpp` (append)

**Interfaces:**
- Consumes: `VaultSettings::migrated_index_version`, `VaultSettings::migrated_probe_caps`, `vault::MIGRATION_INDEX_VERSION` (Task 1).
- Produces: `vault::MigrationScan{ size_t videos; size_t images; uint64_t bytes; bool empty() }`, `vault::migration_pending(const VaultSettings&, uint16_t probe_caps_gen) -> bool`, `vault::scan_migration(const Vault&) -> MigrationScan`, `vault::stamp_migrated(VaultSettings, uint16_t probe_caps_gen) -> VaultSettings`, `media::PROBE_CAPS_GEN`.

`migration_pending` and `stamp_migrated` take the caps generation as a **parameter** rather than reading `media::PROBE_CAPS_GEN` directly — that is what keeps `src/vault/` free of a `src/media/` dependency, and it makes the staleness rule testable without FFmpeg.

- [ ] **Step 1: Write the failing test**

Append to `tests/vault/test_migration.cpp`:

```cpp
#include <filesystem>
#include <string>

#include "vault/migration.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

static const crypto::KdfParams kMigKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> mig_bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

static std::vector<uint8_t> mig_pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

namespace {
struct MigTempVault {
    fs::path path;
    explicit MigTempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_mig_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~MigTempVault()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
    [[nodiscard]] std::string str() const { return path.string(); }
};
} // namespace

TEST(migration_pending_true_for_fresh_zero_watermark)
{
    vault::VaultSettings s = vault::VaultSettings::seeded();
    CHECK(vault::migration_pending(s, 1));
}

TEST(migration_pending_false_once_stamped)
{
    vault::VaultSettings s = vault::stamp_migrated(vault::VaultSettings::seeded(), 1);
    CHECK_EQ(s.migrated_index_version, vault::MIGRATION_INDEX_VERSION);
    CHECK_EQ(s.migrated_probe_caps, 1);
    CHECK(!vault::migration_pending(s, 1));
}

TEST(migration_pending_true_again_when_probe_caps_advance)
{
    vault::VaultSettings s = vault::stamp_migrated(vault::VaultSettings::seeded(), 1);
    CHECK(!vault::migration_pending(s, 1));
    CHECK(vault::migration_pending(s, 2));   // a new codec landed
}

TEST(migration_scan_counts_nothing_for_a_freshly_written_vault)
{
    MigTempVault tv("scan_clean");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("a") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("a", mig_pattern(1000, 1), "one.png") == vault::VaultResult::Ok);

    // A PNG cannot animate, so it is not backfill work; import already probed it.
    const vault::MigrationScan scan = vault::scan_migration(v);
    CHECK_EQ(scan.videos, 0u);
    CHECK_EQ(scan.images, 0u);
    CHECK(scan.empty());
}

TEST(migration_scan_walks_nested_galleries)
{
    MigTempVault tv("scan_nested");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("a") == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("a/b") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("a/b", mig_pattern(1000, 1), "deep.png") == vault::VaultResult::Ok);

    // Nothing to migrate, but the walk must not throw or miss the nesting.
    const vault::MigrationScan scan = vault::scan_migration(v);
    CHECK(scan.empty());
    CHECK_EQ(scan.bytes, 0u);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
scripts/test.sh 2>&1 | grep -E "migration_pending|migration_scan|error:" | head -20
```
Expected: compile error — `vault/migration.h: No such file or directory`.

- [ ] **Step 3: Add `PROBE_CAPS_GEN`**

In `src/media/video_probe.h`, after the `VideoProbeResult` struct:

```cpp
// Phase 65: decode-capability generation. Bump whenever this build can decode a
// container/codec it previously could not (a new FFmpeg decoder enabled, a new
// vendored codec library). A vault whose migrated_probe_caps is below this has
// videos worth re-probing; one at or above it does not, so an undecodable video
// is not re-read on every unlock forever.
inline constexpr uint16_t PROBE_CAPS_GEN = 1;
```

- [ ] **Step 4: Create `src/vault/migration.h`**

```cpp
#pragma once

// Phase 65: what upgrade work a vault still owes, and the watermark that
// records having done it.
//
// Detection is a PURE TREE WALK with zero I/O — no decrypt, no file reads — so
// the unlock-time offer is instant regardless of vault size. Nothing here
// depends on src/media/ or SDL: the caps generation is passed in by the caller
// (see media::PROBE_CAPS_GEN), which keeps vault/ independent of the decoder
// layer and makes the staleness rule testable without FFmpeg.

#include <cstddef>
#include <cstdint>

#include "vault/index.h"

namespace vault {

class Vault;

// Counts of nodes that still owe backfill work, and the plaintext bytes the
// migration would have to read to resolve them.
struct MigrationScan {
    size_t   videos = 0;   // is_video() && vmeta.codec == VideoCodec::Unknown
    size_t   images = 0;   // is_image() && format_can_animate() && !meta.animated
    uint64_t bytes  = 0;   // total orig_size over both arms

    [[nodiscard]] bool empty() const noexcept { return videos == 0 && images == 0; }
    [[nodiscard]] size_t total() const noexcept { return videos + images; }
};

// True when this build knows a backfill this vault has not recorded running.
[[nodiscard]] bool migration_pending(const VaultSettings& s,
                                     uint16_t probe_caps_gen) noexcept;

// Stamp `s` as fully migrated by this build. Returned by value; the caller
// persists it via vault::set_vault_settings.
[[nodiscard]] VaultSettings stamp_migrated(VaultSettings s,
                                           uint16_t probe_caps_gen) noexcept;

// Walk the whole tree and count outstanding work. Main-thread only (touches the
// index tree via Vault::list). No I/O.
[[nodiscard]] MigrationScan scan_migration(const Vault& v);

} // namespace vault
```

- [ ] **Step 5: Create `src/vault/migration.cpp`**

```cpp
#include "vault/migration.h"

#include <string>

#include "vault/vault.h"

namespace vault {
namespace {

void walk(const Vault& v, const std::string& path, MigrationScan& out)
{
    for (const IndexNode* n : v.list(path)) {
        const std::string child = path.empty() ? n->name : path + "/" + n->name;
        if (n->is_gallery()) {
            walk(v, child, out);
            continue;
        }
        if (n->is_video()) {
            if (n->vmeta.codec == VideoCodec::Unknown) {
                ++out.videos;
                out.bytes += n->vmeta.orig_size;
            }
            continue;
        }
        // The animated arm deliberately over-counts: a genuinely static GIF is
        // indistinguishable from an un-backfilled one without decrypting it, so
        // it IS work. The watermark, not this walk, prevents recurrence.
        if (n->is_image() && format_can_animate(n->meta.format) && !n->meta.animated) {
            ++out.images;
            out.bytes += n->meta.orig_size;
        }
    }
}

} // namespace

bool migration_pending(const VaultSettings& s, uint16_t probe_caps_gen) noexcept
{
    return s.migrated_index_version < MIGRATION_INDEX_VERSION ||
           s.migrated_probe_caps    < probe_caps_gen;
}

VaultSettings stamp_migrated(VaultSettings s, uint16_t probe_caps_gen) noexcept
{
    s.migrated_index_version = MIGRATION_INDEX_VERSION;
    s.migrated_probe_caps    = probe_caps_gen;
    return s;
}

MigrationScan scan_migration(const Vault& v)
{
    MigrationScan scan;
    walk(v, "", scan);
    return scan;
}

} // namespace vault
```

- [ ] **Step 6: Regenerate the build files and run the tests**

```bash
scripts/gen.sh && scripts/test.sh 2>&1 | tail -20
```
Expected: all tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/vault/migration.h src/vault/migration.cpp src/media/video_probe.h tests/vault/test_migration.cpp
git commit -m "feat(vault): migration detection scan + probe caps generation"
```

---

### Task 3: Commit-free apply APIs on `Vault`

The existing `repair_video_metadata` / `repair_image_animated` do read + probe + **commit** in one call. The migration needs the probe split off (it happens on a pool thread) and the commit deferred to a single batch. This task adds the apply half.

**Files:**
- Modify: `src/vault/vault.h` (friend declarations near `remove_media_batch` at :302; free functions near :432)
- Modify: `src/vault/vault.cpp` (implement beside `repair_video_metadata` at :954)
- Test: `tests/vault/test_migration.cpp` (append)

**Interfaces:**
- Consumes: `vault::stamp_migrated` (Task 2).
- Produces:
  - `struct vault::VideoProbeApply { VideoCodec codec; uint32_t width, height; uint64_t duration_us; std::span<const uint8_t> poster_jpeg; }`
  - `vault::apply_video_probe(Vault&, std::string_view node_path, const VideoProbeApply&) -> VaultResult`
  - `vault::apply_image_animated(Vault&, std::string_view node_path, bool animated) -> VaultResult`
  - `vault::commit_migration(Vault&, VaultSettings) -> VaultResult`

`VideoProbeApply` deliberately mirrors `media::VideoProbeResult` field-for-field rather than reusing it: `vault.h` must not include `media/video_probe.h` (Global Constraints — layering). The caller copies the four scalars and hands a span at the poster.

- [ ] **Step 1: Write the failing test**

Append to `tests/vault/test_migration.cpp`:

```cpp
TEST(apply_image_animated_defers_the_commit)
{
    MigTempVault tv("apply_anim");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", mig_pattern(1000, 1), "a.png") == vault::VaultResult::Ok);

    // A PNG cannot animate: the apply is a well-formed no-op, not an error.
    CHECK(vault::apply_image_animated(v, "a.png", true) == vault::VaultResult::Ok);

    CHECK(vault::apply_image_animated(v, "missing.png", true)
          == vault::VaultResult::NotFound);
}

TEST(commit_migration_persists_the_watermark)
{
    MigTempVault tv("commit_wm");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), mig_bytes("pw"), {}, kMigKdf, v)
                == vault::VaultResult::Ok);
        CHECK(vault::migration_pending(vault::vault_settings(v), 1));

        const vault::VaultSettings stamped =
            vault::stamp_migrated(vault::vault_settings(v), 1);
        REQUIRE(vault::commit_migration(v, stamped) == vault::VaultResult::Ok);
        CHECK(!vault::migration_pending(vault::vault_settings(v), 1));
    }
    // Survives a close/reopen — this is the whole point of the watermark.
    {
        vault::Vault v;
        REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
        REQUIRE(v.unlock(mig_bytes("pw"), {}) == vault::VaultResult::Ok);
        CHECK(!vault::migration_pending(vault::vault_settings(v), 1));
        CHECK(vault::migration_pending(vault::vault_settings(v), 2));
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
scripts/test.sh 2>&1 | grep -E "apply_image_animated|commit_migration|error:" | head -20
```
Expected: compile error — `'apply_image_animated' is not a member of 'vault'`.

- [ ] **Step 3: Declare the friends and free functions in `src/vault/vault.h`**

Inside `class Vault`, beside the other friends (near :302):

```cpp
    // Phase 65 migration: apply probed metadata WITHOUT committing, so a whole
    // migration pass costs one index write instead of one per node.
    friend VaultResult apply_video_probe(Vault& v, std::string_view node_path,
                                         const VideoProbeApply& probe);
    friend VaultResult apply_image_animated(Vault& v, std::string_view node_path,
                                            bool animated);
    friend VaultResult commit_migration(Vault& v, VaultSettings settings);
```

Above the class (beside the other vault-level structs), add:

```cpp
// Probed video metadata handed back from a migration worker. Mirrors
// media::VideoProbeResult field-for-field ON PURPOSE: vault.h must not include
// media/video_probe.h (vault/ does not depend on media/), and the poster is
// borrowed as a span rather than owned.
struct VideoProbeApply {
    VideoCodec               codec       = VideoCodec::Unknown;
    uint32_t                 width       = 0;
    uint32_t                 height      = 0;
    uint64_t                 duration_us = 0;
    std::span<const uint8_t> poster_jpeg;   // empty = leave the poster alone
};
```

And near the other free functions (after `remove_media_batch` at :432):

```cpp
// Write probed metadata onto a video node and append its poster chunk if the
// node has none, WITHOUT committing the index. Locked if locked; NotFound if
// the path does not resolve to a video; IoError if the poster append fails.
// A node that already has a real codec is left alone (Ok, no write).
// Coordinator-thread only — mutates the tree and appends to fp_.
[[nodiscard]] VaultResult apply_video_probe(Vault& v, std::string_view node_path,
                                            const VideoProbeApply& probe);

// Set an image node's animated flag WITHOUT committing the index. Ok (no write)
// when the flag is already correct or the format cannot animate; NotFound if the
// path does not resolve to an image. Coordinator-thread only.
[[nodiscard]] VaultResult apply_image_animated(Vault& v, std::string_view node_path,
                                               bool animated);

// Persist `settings` (carrying the Phase 65 watermark) together with every
// pending apply_* mutation in ONE commit_index(). Locked if locked; IoError if
// the commit fails (the tree is already mutated — the remove_media_batch
// contract).
[[nodiscard]] VaultResult commit_migration(Vault& v, VaultSettings settings);
```

- [ ] **Step 4: Implement them in `src/vault/vault.cpp`**

Place directly after `repair_image_animated`:

```cpp
VaultResult apply_video_probe(Vault& v, std::string_view node_path,
                              const VideoProbeApply& probe)
{
    using enum VaultResult;
    if (!v.unlocked_) return Locked;

    IndexNode* n = v.resolve_node(node_path);
    if (!n || !n->is_video()) return NotFound;
    if (n->vmeta.codec != VideoCodec::Unknown) return Ok;   // already real metadata
    if (probe.codec == VideoCodec::Unknown) return Ok;      // nothing learned

    n->vmeta.codec       = probe.codec;
    n->vmeta.width       = probe.width;
    n->vmeta.height      = probe.height;
    n->vmeta.duration_us = probe.duration_us;

    if (n->vmeta.poster_length == 0 && !probe.poster_jpeg.empty()) {
        // Phase 50 protocol: every append to fp_ holds write_mutex_.
        std::lock_guard lk(*v.write_mutex_);
        ChunkStore store(v.fp_, v.master_key_.as_span(), framed_chunks(v.header_));
        ChunkSpan poster_span;
        if (!store.append_chunk(probe.poster_jpeg, poster_span)) return IoError;
        if (!store.sync()) return IoError;
        n->vmeta.poster_offset = poster_span.offset;
        n->vmeta.poster_length = poster_span.length;
    }
    return Ok;
}

VaultResult apply_image_animated(Vault& v, std::string_view node_path, bool animated)
{
    using enum VaultResult;
    if (!v.unlocked_) return Locked;

    IndexNode* n = v.resolve_node(node_path);
    if (!n || !n->is_image()) return NotFound;
    if (!format_can_animate(n->meta.format)) return Ok;
    if (n->meta.animated == animated) return Ok;

    n->meta.animated = animated;
    return Ok;
}

VaultResult commit_migration(Vault& v, VaultSettings settings)
{
    using enum VaultResult;
    if (!v.unlocked_) return Locked;
    v.settings_ = std::move(settings);
    return v.commit_index();
}
```

If the settings member is not named `settings_`, use whatever `vault_settings()` returns a reference to — check `src/vault/vault.h:453` and its definition in `vault.cpp` and match it.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
scripts/test.sh 2>&1 | tail -20
```
Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/vault/vault.h src/vault/vault.cpp tests/vault/test_migration.cpp
git commit -m "feat(vault): commit-free apply APIs for batched migration"
```

---

### Task 4: `MigrationJob` — single-threaded pass

Correctness first, parallelism second. This task builds the whole pass on one coordinator thread with no pool, so the semantics (work list, apply, single commit, cancel) are proven before concurrency is added in Task 5.

**Files:**
- Create: `src/ui/migration_job.h`, `src/ui/migration_job.cpp`
- Test: `tests/ui/test_migration_job.cpp` (create)

**Interfaces:**
- Consumes: `vault::scan_migration`, `vault::stamp_migrated`, `vault::migration_pending` (Task 2); `vault::apply_video_probe`, `vault::apply_image_animated`, `vault::commit_migration`, `vault::VideoProbeApply` (Task 3); `vault::read_thumb_span` (`src/vault/vault.h:424`); `media::probe_video`, `media::PROBE_CAPS_GEN`; `image::is_animated`.
- Produces: `ui::MigrationPhase`, `ui::MigrationOutcome`, `ui::MigrationJob` with `start`, `active`, `total`, `done`, `phase`, `cancel`, `take_outcome`.

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_migration_job.cpp`:

```cpp
#include "test_framework.h"

#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "ui/migration_job.h"
#include "vault/migration.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

static const crypto::KdfParams kJobKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> job_bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

static std::vector<uint8_t> job_pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

namespace {
struct JobTempVault {
    fs::path path;
    explicit JobTempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_job_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~JobTempVault()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
    [[nodiscard]] std::string str() const { return path.string(); }
};

// Drive the job to completion the way the UI does: poll, then collect once.
ui::MigrationOutcome run_to_completion(ui::MigrationJob& job)
{
    while (job.active()) {
        if (auto out = job.take_outcome()) return *out;
        std::this_thread::yield();
    }
    if (auto out = job.take_outcome()) return *out;
    return {};
}
} // namespace

TEST(migration_job_on_clean_vault_stamps_watermark_and_does_nothing_else)
{
    JobTempVault tv("clean");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", job_pattern(1000, 1), "a.png") == vault::VaultResult::Ok);

    ui::MigrationJob job;
    REQUIRE(job.start(v));
    const ui::MigrationOutcome out = run_to_completion(job);

    CHECK(out.ok);
    CHECK(!out.cancelled);
    CHECK_EQ(out.total, 0);
    CHECK_EQ(out.videos_fixed, 0);
    CHECK_EQ(out.images_fixed, 0);
    CHECK(!vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN));
}

TEST(migration_job_watermark_survives_reopen)
{
    JobTempVault tv("persist");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
                == vault::VaultResult::Ok);
        ui::MigrationJob job;
        REQUIRE(job.start(v));
        const ui::MigrationOutcome out = run_to_completion(job);
        CHECK(out.ok);
    }
    {
        vault::Vault v;
        REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
        REQUIRE(v.unlock(job_bytes("pw"), {}) == vault::VaultResult::Ok);
        CHECK(!vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN));
    }
}

TEST(migration_job_double_start_is_rejected)
{
    JobTempVault tv("double");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);

    ui::MigrationJob job;
    REQUIRE(job.start(v));
    CHECK(!job.start(v));            // already in flight
    (void)run_to_completion(job);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
scripts/test.sh 2>&1 | grep -E "migration_job|error:" | head -20
```
Expected: compile error — `ui/migration_job.h: No such file or directory`.

- [ ] **Step 3: Create `src/ui/migration_job.h`**

```cpp
#pragma once

// Phase 65: the one-time blocking vault migration.
//
// Threading contract (FileOpJob's, NOT the Phase 50 staging contract): while
// active(), this job owns the vault EXCLUSIVELY. The owning screen must not
// read the vault — no thumbnail decrypt, no listing — until take_outcome()
// returns; it only polls progress and draws a modal. That exclusivity is what
// lets the coordinator mutate the index tree directly, which a background
// import (running concurrently with browsing) may never do.
//
// Cancel stops between items. Work applied so far is committed and durable, but
// the watermark is NOT stamped and compaction is skipped, so the migration is
// re-offered at the next unlock.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "vault/op_progress.h"

namespace vault { class Vault; }

namespace ui {

// Coarse stage, for the modal's label. Progress counters are per-stage.
enum class MigrationPhase { Idle, Scanning, Repairing, Committing, Compacting, Done };

struct MigrationOutcome {
    bool        ok        = false;  // ran to completion (or a clean cancel)
    bool        cancelled = false;
    int         videos_fixed   = 0; // codec resolved and metadata written
    int         videos_skipped = 0; // still undecodable — watermark still advances
    int         images_fixed   = 0; // animated flag corrected
    int         failed         = 0; // read/decrypt failures
    int         total          = 0; // items attempted
    uint64_t    reclaimed_bytes = 0;// freed by the compaction phase
    std::string status;             // human-facing summary (never any content)
    std::string error;              // set when ok == false
};

class MigrationJob {
public:
    MigrationJob() = default;
    ~MigrationJob();

    MigrationJob(const MigrationJob&)            = delete;
    MigrationJob& operator=(const MigrationJob&) = delete;

    // Spawn the coordinator. False if a job is already in flight. `v` must
    // outlive the job and must not be touched by anyone else until
    // take_outcome() returns.
    bool start(vault::Vault& v);

    [[nodiscard]] bool active() const noexcept { return active_.load(); }
    [[nodiscard]] int  total()  const noexcept { return progress_.total.load(); }
    [[nodiscard]] int  done()   const noexcept { return progress_.done.load(); }
    [[nodiscard]] MigrationPhase phase() const noexcept { return phase_.load(); }

    void cancel() noexcept { progress_.cancel.store(true); }

    // Join and hand back the outcome exactly once; nullopt while still running.
    [[nodiscard]] std::optional<MigrationOutcome> take_outcome();

private:
    void run(vault::Vault& v);

    vault::OpProgress            progress_;
    std::atomic<bool>            active_{false};
    std::atomic<bool>            done_{false};
    std::atomic<MigrationPhase>  phase_{MigrationPhase::Idle};
    MigrationOutcome             outcome_;   // written by worker, read after join
    std::jthread                 thread_;
};

} // namespace ui
```

- [ ] **Step 4: Create `src/ui/migration_job.cpp` (single-threaded pass)**

```cpp
#include "ui/migration_job.h"

#include <cstring>
#include <utility>
#include <vector>

#include "crypto/secure_mem.h"
#include "image/anim_info.h"
#include "image/image.h"
#include "media/video_probe.h"
#include "vault/index.h"
#include "vault/migration.h"
#include "vault/vault.h"

namespace ui {
namespace {

// One unit of migration work, holding COPIED chunk spans rather than an
// IndexNode* — the pool added in Task 5 must never hold a tree pointer.
struct Item {
    std::string node_path;
    bool        is_video = false;
    std::vector<std::pair<uint64_t, uint64_t>> data_spans;
    uint64_t    bytes      = 0;
    uint32_t    chunk_size = 0;   // videos only
    uint8_t     format     = 0;   // images only
};

// What a worker learned. `ok == false` means the read or decode failed.
struct Result {
    size_t   index = 0;
    bool     ok    = false;
    bool     resolved = false;    // video: a real codec came back
    vault::VideoCodec codec = vault::VideoCodec::Unknown;
    uint32_t width = 0, height = 0;
    uint64_t duration_us = 0;
    std::vector<uint8_t> poster_jpeg;
    bool     animated = false;    // images only
};

void collect(const vault::Vault& v, const std::string& path, std::vector<Item>& out)
{
    for (const vault::IndexNode* n : v.list(path)) {
        const std::string child = path.empty() ? n->name : path + "/" + n->name;
        if (n->is_gallery()) { collect(v, child, out); continue; }

        if (n->is_video() && n->vmeta.codec == vault::VideoCodec::Unknown) {
            Item it;
            it.node_path  = child;
            it.is_video   = true;
            it.bytes      = n->vmeta.orig_size;
            it.chunk_size = n->vmeta.chunk_size;
            it.data_spans.reserve(n->vmeta.chunks.size());
            for (const vault::VideoChunk& c : n->vmeta.chunks)
                it.data_spans.emplace_back(c.offset, c.length);
            out.push_back(std::move(it));
            continue;
        }
        if (n->is_image() && vault::format_can_animate(n->meta.format) &&
            !n->meta.animated) {
            Item it;
            it.node_path  = child;
            it.is_video   = false;
            it.bytes      = n->meta.orig_size;
            it.format     = n->meta.format;
            it.data_spans = {{n->meta.data_offset, n->meta.data_length}};
            out.push_back(std::move(it));
        }
    }
}

// Concatenate an item's chunk spans into mlock'd memory using ONLY
// vault::read_thumb_span — the any-thread-safe decrypt path. Never touches
// read_fp_ or the tree.
bool read_item(const vault::Vault& v, const Item& it, crypto::SecureBytes& out)
{
    if (it.data_spans.size() == 1) {
        const auto& [off, len] = it.data_spans[0];
        return vault::read_thumb_span(v, off, len, out) == vault::VaultResult::Ok;
    }
    std::vector<uint8_t> joined;          // wiped below via SecureBytes copy
    joined.reserve(static_cast<size_t>(it.bytes));
    crypto::SecureBytes chunk;
    for (const auto& [off, len] : it.data_spans) {
        if (vault::read_thumb_span(v, off, len, chunk) != vault::VaultResult::Ok) {
            crypto::wipe(std::span<uint8_t>(joined));
            return false;
        }
        joined.insert(joined.end(), chunk.data(), chunk.data() + chunk.size());
    }
    const bool ok = out.assign(std::span<const uint8_t>(joined));
    crypto::wipe(std::span<uint8_t>(joined));
    return ok;
}

// Pure CPU stage. Safe on any thread: reads only through read_thumb_span,
// touches no tree node.
Result process(const vault::Vault& v, const Item& it, size_t index)
{
    Result r;
    r.index = index;

    crypto::SecureBytes data;
    if (!read_item(v, it, data)) return r;   // ok stays false

    if (it.is_video) {
        media::VideoProbeResult probe;
        if (media::probe_video(data.as_span(), probe) &&
            probe.codec != vault::VideoCodec::Unknown) {
            r.resolved    = true;
            r.codec       = probe.codec;
            r.width       = probe.width;
            r.height      = probe.height;
            r.duration_us = probe.duration_us;
            r.poster_jpeg = std::move(probe.poster_jpeg);
        }
        r.ok = true;   // a clean "still undecodable" is a success, not a failure
        return r;
    }

    r.animated = image::is_animated(static_cast<image::ImageFormat>(it.format),
                                    data.as_span());
    r.ok = true;
    return r;
}

} // namespace

MigrationJob::~MigrationJob() = default;

bool MigrationJob::start(vault::Vault& v)
{
    if (active_.load()) return false;
    progress_.total.store(0);
    progress_.done.store(0);
    progress_.cancel.store(false);
    phase_.store(MigrationPhase::Scanning);
    outcome_ = MigrationOutcome{};
    done_.store(false);
    active_.store(true);
    thread_ = std::jthread([this, &v] { run(v); });
    return true;
}

void MigrationJob::run(vault::Vault& v)
{
    MigrationOutcome out;

    std::vector<Item> items;
    collect(v, "", items);
    out.total = static_cast<int>(items.size());
    progress_.total.store(out.total);

    phase_.store(MigrationPhase::Repairing);
    for (size_t i = 0; i < items.size(); ++i) {
        if (progress_.cancel.load()) { out.cancelled = true; break; }

        const Result r = process(v, items[i], i);
        if (!r.ok) {
            ++out.failed;
        } else if (items[i].is_video) {
            if (r.resolved) {
                const vault::VideoProbeApply apply{
                    .codec       = r.codec,
                    .width       = r.width,
                    .height      = r.height,
                    .duration_us = r.duration_us,
                    .poster_jpeg = std::span<const uint8_t>(r.poster_jpeg),
                };
                if (vault::apply_video_probe(v, items[i].node_path, apply) ==
                    vault::VaultResult::Ok) {
                    ++out.videos_fixed;
                } else {
                    ++out.failed;
                }
            } else {
                ++out.videos_skipped;
            }
        } else {
            if (vault::apply_image_animated(v, items[i].node_path, r.animated) ==
                vault::VaultResult::Ok) {
                ++out.images_fixed;
            } else {
                ++out.failed;
            }
        }
        progress_.done.store(static_cast<int>(i + 1));
    }

    phase_.store(MigrationPhase::Committing);
    // The watermark is stamped ONLY on a full pass. A cancel still commits the
    // work already applied — it is correct and durable — but leaves the vault
    // marked un-migrated so the next unlock re-offers it.
    vault::VaultSettings settings = vault::vault_settings(v);
    if (!out.cancelled) {
        settings = vault::stamp_migrated(settings, media::PROBE_CAPS_GEN);
    }
    if (const auto res = vault::commit_migration(v, settings);
        res != vault::VaultResult::Ok) {
        out.ok    = false;
        out.error = "Migration could not be saved; the vault is unchanged.";
        phase_.store(MigrationPhase::Done);
        outcome_ = std::move(out);
        done_.store(true);
        return;
    }

    out.ok = true;
    phase_.store(MigrationPhase::Done);
    outcome_ = std::move(out);
    done_.store(true);
}

std::optional<MigrationOutcome> MigrationJob::take_outcome()
{
    if (!active_.load() || !done_.load()) return std::nullopt;
    if (thread_.joinable()) thread_.join();
    active_.store(false);
    done_.store(false);
    return std::move(outcome_);
}

} // namespace ui
```

If `crypto::wipe` or `crypto::SecureBytes::assign` do not exist under those exact names, check `src/crypto/secure_mem.h` and use the real ones — do not invent an API.

- [ ] **Step 5: Regenerate and run the tests**

```bash
scripts/gen.sh && scripts/test.sh 2>&1 | tail -20
```
Expected: all tests pass, including the three `migration_job_*` tests.

- [ ] **Step 6: Run under ASAN**

```bash
scripts/test.sh --asan 2>&1 | tail -20
```
Expected: pass, no findings. This exercises the decrypt + concat path.

- [ ] **Step 7: Commit**

```bash
git add src/ui/migration_job.h src/ui/migration_job.cpp tests/ui/test_migration_job.cpp
git commit -m "feat(ui): MigrationJob single-threaded pass with batched commit"
```

---

### Task 5: Video and animated arms end-to-end

Task 4 proved the plumbing on a vault with no work. This task proves both arms actually repair, using the existing test-only seam.

**Files:**
- Test: `tests/ui/test_migration_job.cpp` (append)
- Modify: `src/ui/migration_job.cpp` only if a test exposes a defect

**Interfaces:**
- Consumes: everything from Task 4; `test_only_force_video_codec_unknown` (`src/vault/vault.h:224`, defined in `tests/vault/test_video.cpp`).
- Produces: no new API.

- [ ] **Step 1: Write the failing tests**

Append to `tests/ui/test_migration_job.cpp`. The video test must be gated on the vendored-AV build, matching how `tests/vault/test_video.cpp` gates its own cases — check that file for the exact macro and mirror it.

```cpp
#if defined(OSV_VENDORED_AV)
// Declared in tests/vault/test_video.cpp — resets a decodable video back to the
// Unknown state a pre-Phase-40 import would have left it in.
namespace vault { void test_only_force_video_codec_unknown(Vault& v, std::string_view p); }

TEST(migration_job_repairs_unknown_video_metadata)
{
    JobTempVault tv("video");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);

    // Reuse the same fixture tests/vault/test_video.cpp imports; look up its
    // path there (OSV_MEDIA_FIXTURE_DIR) and use the identical file.
    const std::vector<uint8_t> mp4 = read_media_fixture("sample.mp4");
    REQUIRE(v.add_video("", mp4, "clip.mp4") == vault::VaultResult::Ok);
    vault::test_only_force_video_codec_unknown(v, "clip.mp4");

    const vault::MigrationScan before = vault::scan_migration(v);
    CHECK_EQ(before.videos, 1u);

    ui::MigrationJob job;
    REQUIRE(job.start(v));
    const ui::MigrationOutcome out = run_to_completion(job);

    CHECK(out.ok);
    CHECK_EQ(out.videos_fixed, 1);
    CHECK_EQ(out.failed, 0);

    const vault::MigrationScan after = vault::scan_migration(v);
    CHECK_EQ(after.videos, 0u);

    const std::vector<const vault::IndexNode*> kids = v.list("");
    REQUIRE(kids.size() == 1);
    CHECK(kids[0]->vmeta.codec != vault::VideoCodec::Unknown);
    CHECK(kids[0]->vmeta.width > 0);
    CHECK(kids[0]->vmeta.poster_length > 0);
}

TEST(migration_job_is_not_reoffered_after_a_full_pass)
{
    JobTempVault tv("once");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);
    const std::vector<uint8_t> mp4 = read_media_fixture("sample.mp4");
    REQUIRE(v.add_video("", mp4, "clip.mp4") == vault::VaultResult::Ok);
    vault::test_only_force_video_codec_unknown(v, "clip.mp4");

    ui::MigrationJob job;
    REQUIRE(job.start(v));
    (void)run_to_completion(job);

    CHECK(!vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN));
    CHECK(vault::scan_migration(v).empty());
}
#endif // OSV_VENDORED_AV

TEST(migration_job_flips_an_animated_gif_and_leaves_a_static_one)
{
    JobTempVault tv("gif");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);

    // Fixtures already used by tests/image/ for Phase 47 — reuse the same two
    // files rather than synthesising GIF bytes here.
    const std::vector<uint8_t> anim   = read_image_fixture("animated.gif");
    const std::vector<uint8_t> static_ = read_image_fixture("static.gif");
    REQUIRE(v.add_image("", anim,    "anim.gif")   == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", static_, "static.gif") == vault::VaultResult::Ok);

    // Import already sets the flag correctly, so force both to the pre-v7 state
    // the migration is meant to repair.
    REQUIRE(vault::apply_image_animated(v, "anim.gif", false) == vault::VaultResult::Ok);

    ui::MigrationJob job;
    REQUIRE(job.start(v));
    const ui::MigrationOutcome out = run_to_completion(job);
    CHECK(out.ok);

    for (const vault::IndexNode* n : v.list("")) {
        if (n->name == "anim.gif")   CHECK(n->meta.animated);
        if (n->name == "static.gif") CHECK(!n->meta.animated);
    }
}

TEST(migration_job_cancel_leaves_watermark_unset)
{
    JobTempVault tv("cancel");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);

    // Enough items that a cancel reliably lands MID-pass. Cancelling an empty
    // vault would assert nothing: the repair loop never runs, `cancelled` stays
    // false, and the watermark is stamped exactly as on a clean full pass.
    const std::vector<uint8_t> anim = read_image_fixture("animated.gif");
    constexpr int kCount = 64;
    for (int i = 0; i < kCount; ++i) {
        const std::string name = "c" + std::to_string(i) + ".gif";
        REQUIRE(v.add_image("", anim, name) == vault::VaultResult::Ok);
        REQUIRE(vault::apply_image_animated(v, name, false) == vault::VaultResult::Ok);
    }

    ui::MigrationJob job;
    REQUIRE(job.start(v));
    // Cancel immediately. Deliberately NOT "spin until done() > 0 then cancel":
    // that spin exits when the job finishes, turning cancel() into a no-op and
    // making the assertions below fail. Cancelling at once only requires the
    // flag to arrive before 64 decrypt+decode items finish, which the loop
    // checks at the top of every iteration.
    job.cancel();
    const ui::MigrationOutcome out = run_to_completion(job);

    // A cancel is a clean partial: committed, durable, readable — but NOT
    // stamped, so the next unlock re-offers the migration. These assertions are
    // unconditional on purpose.
    CHECK(out.ok);
    CHECK(out.cancelled);
    CHECK(vault::migration_pending(vault::vault_settings(v), media::PROBE_CAPS_GEN));
    CHECK(out.images_fixed < kCount);   // stopped early, did not silently finish
}
```

`read_media_fixture` / `read_image_fixture` are placeholders for whatever the existing suites use — find the real helpers in `tests/vault/test_video.cpp` and `tests/image/`, and call those. If no shared helper exists, write a small local one using `OSV_MEDIA_FIXTURE_DIR` / `OSV_FIXTURE_DIR` exactly as those files do. Do not invent a new fixture format, and do not add new fixture binaries — reuse the files already committed.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
scripts/test.sh 2>&1 | grep -E "migration_job_repairs|migration_job_flips|error:" | head -20
```
Expected: either a compile error on the fixture helper (fix by using the real one), or assertion failures.

- [ ] **Step 3: Fix whatever the tests expose**

The Task 4 implementation is expected to pass these as written. If the video arm fails, the most likely cause is `read_item` mis-assembling multi-chunk videos — compare its chunk mapping against `VideoStream::read` in `src/ui/dup_scan.cpp:288`, which solves the identical problem.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
scripts/test.sh 2>&1 | tail -20
```
Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add tests/ui/test_migration_job.cpp src/ui/migration_job.cpp
git commit -m "test(ui): migration repairs both arms end-to-end"
```

---

### Task 6: Parallel decode pool

**Files:**
- Modify: `src/ui/migration_job.cpp` (replace the serial loop in `run`)
- Test: `tests/ui/test_migration_job.cpp` (append)

**Interfaces:**
- Consumes: `Item`, `Result`, `process` (Task 4 internals).
- Produces: no public API change — `MigrationJob`'s interface is identical. This is why Task 4 built it serially first: the pool is a pure internal swap, and every existing test is the regression suite for it.

- [ ] **Step 1: Write the failing test**

Append to `tests/ui/test_migration_job.cpp`:

```cpp
TEST(migration_job_pool_handles_many_items_without_loss)
{
    JobTempVault tv("pool");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);

    const std::vector<uint8_t> anim = read_image_fixture("animated.gif");
    constexpr int kCount = 64;
    for (int i = 0; i < kCount; ++i) {
        const std::string name = "g" + std::to_string(i) + ".gif";
        REQUIRE(v.add_image("", anim, name) == vault::VaultResult::Ok);
        REQUIRE(vault::apply_image_animated(v, name, false) == vault::VaultResult::Ok);
    }

    ui::MigrationJob job;
    REQUIRE(job.start(v));
    const ui::MigrationOutcome out = run_to_completion(job);

    CHECK(out.ok);
    CHECK_EQ(out.total, kCount);
    CHECK_EQ(out.images_fixed, kCount);   // every item applied, none dropped
    CHECK_EQ(out.failed, 0);
    CHECK_EQ(job.done(), kCount);         // progress reached the denominator

    for (const vault::IndexNode* n : v.list("")) CHECK(n->meta.animated);
}
```

- [ ] **Step 2: Run the test to verify it fails or is merely slow**

```bash
scripts/test.sh 2>&1 | grep -E "migration_job_pool|error:" | head -20
```
Expected: passes serially but slowly. That is fine — it is the correctness net for the swap. It must still pass after Step 3, which is the real gate.

- [ ] **Step 3: Replace the serial loop with a bounded producer/consumer pool**

In `src/ui/migration_job.cpp`, add to the anonymous namespace:

```cpp
// Bounded result queue. The bound is not hygiene: decoded frames and encoded
// posters live in mlock'd SecureBytes and the process budget is 256 MiB
// (platform::grow_secure_mem_budget), so an unbounded queue would exhaust the
// lockable pool and start failing locks mid-migration.
class ResultQueue {
public:
    explicit ResultQueue(size_t cap) : cap_(cap) {}

    void push(Result r)
    {
        std::unique_lock lk(m_);
        not_full_.wait(lk, [this] { return q_.size() < cap_ || closed_; });
        q_.push_back(std::move(r));
        lk.unlock();
        not_empty_.notify_one();
    }

    bool pop(Result& out)
    {
        std::unique_lock lk(m_);
        not_empty_.wait(lk, [this] { return !q_.empty() || closed_; });
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop_front();
        lk.unlock();
        not_full_.notify_one();
        return true;
    }

    void close()
    {
        { std::lock_guard lk(m_); closed_ = true; }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

private:
    std::mutex              m_;
    std::condition_variable not_empty_, not_full_;
    std::deque<Result>      q_;
    size_t                  cap_;
    bool                    closed_ = false;
};
```

Add `#include <condition_variable>`, `#include <deque>`, and `#include <algorithm>` at the top.

Replace the serial `for` loop in `run` with:

```cpp
    phase_.store(MigrationPhase::Repairing);

    const unsigned hw = std::thread::hardware_concurrency();
    const size_t workers =
        std::min<size_t>(items.size(), std::max<unsigned>(1u, hw > 1 ? hw - 1 : 1u));

    if (!items.empty()) {
        ResultQueue results(std::max<size_t>(2, workers * 2));
        std::atomic<size_t> next{0};

        std::vector<std::jthread> pool;
        pool.reserve(workers);
        for (size_t w = 0; w < workers; ++w) {
            pool.emplace_back([&] {
                for (;;) {
                    const size_t i = next.fetch_add(1);
                    if (i >= items.size() || progress_.cancel.load()) break;
                    results.push(process(v, items[i], i));
                }
            });
        }

        // The coordinator is the ONLY thread that touches the tree or fp_.
        int collected = 0;
        const int expect = static_cast<int>(items.size());
        Result r;
        while (collected < expect && results.pop(r)) {
            apply_one(v, items[r.index], r, out);
            ++collected;
            progress_.done.store(collected);
            if (progress_.cancel.load()) { out.cancelled = true; break; }
        }
        results.close();
        for (auto& t : pool) if (t.joinable()) t.join();
    }
```

Extract the per-item apply from the old loop into a free function in the anonymous namespace, so both the queue drain and the tests exercise identical logic:

```cpp
// Coordinator-thread only: mutates the tree and may append to fp_.
void apply_one(vault::Vault& v, const Item& it, const Result& r, MigrationOutcome& out)
{
    if (!r.ok) { ++out.failed; return; }

    if (it.is_video) {
        if (!r.resolved) { ++out.videos_skipped; return; }
        const vault::VideoProbeApply apply{
            .codec       = r.codec,
            .width       = r.width,
            .height      = r.height,
            .duration_us = r.duration_us,
            .poster_jpeg = std::span<const uint8_t>(r.poster_jpeg),
        };
        if (vault::apply_video_probe(v, it.node_path, apply) == vault::VaultResult::Ok)
            ++out.videos_fixed;
        else
            ++out.failed;
        return;
    }

    if (vault::apply_image_animated(v, it.node_path, r.animated) ==
        vault::VaultResult::Ok)
        ++out.images_fixed;
    else
        ++out.failed;
}
```

- [ ] **Step 4: Run the full suite**

```bash
scripts/test.sh 2>&1 | tail -20
```
Expected: all tests pass, including every Task 4 and Task 5 test unchanged.

- [ ] **Step 5: Run under ThreadSanitizer — this is the real gate**

This is the first worker pool in the codebase. TSAN is not a formality here.

```bash
scripts/test.sh --tsan 2>&1 | tail -40
```
Expected: pass with zero data-race reports. If TSAN flags a race on the tree, a worker is touching an `IndexNode*` — `Item` must hold only copied spans and a path string.

- [ ] **Step 6: Run under ASAN**

```bash
scripts/test.sh --asan 2>&1 | tail -20
```
Expected: pass, no findings.

- [ ] **Step 7: Commit**

```bash
git add src/ui/migration_job.cpp tests/ui/test_migration_job.cpp
git commit -m "perf(ui): parallel decode pool for the migration pass"
```

---

### Task 7: Compaction phase

**Files:**
- Modify: `src/ui/migration_job.cpp` (`run`, after the commit)
- Test: `tests/ui/test_migration_job.cpp` (append)

**Interfaces:**
- Consumes: `Vault::compact(OpProgress*)` (`src/vault/vault.h:341`), `Vault::wasted_bytes()` (`src/vault/vault.h:328`).
- Produces: `MigrationOutcome::reclaimed_bytes` is populated.

- [ ] **Step 1: Write the failing test**

```cpp
TEST(migration_job_skips_compaction_when_nothing_is_wasted)
{
    JobTempVault tv("nocompact");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", job_pattern(1000, 1), "a.png") == vault::VaultResult::Ok);

    ui::MigrationJob job;
    REQUIRE(job.start(v));
    const ui::MigrationOutcome out = run_to_completion(job);

    CHECK(out.ok);
    CHECK_EQ(out.reclaimed_bytes, 0u);
    // The vault must still be readable and intact afterwards.
    CHECK_EQ(v.list("").size(), 1u);
}

TEST(migration_job_cancel_skips_compaction)
{
    JobTempVault tv("cancelcompact");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), job_bytes("pw"), {}, kJobKdf, v)
            == vault::VaultResult::Ok);

    ui::MigrationJob job;
    REQUIRE(job.start(v));
    job.cancel();
    const ui::MigrationOutcome out = run_to_completion(job);

    CHECK_EQ(out.reclaimed_bytes, 0u);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
scripts/test.sh 2>&1 | grep -E "skips_compaction|error:" | head -20
```
Expected: compile error — `'reclaimed_bytes'` unused/uninitialised path, or an assertion failure.

- [ ] **Step 3: Add the compaction phase**

In `run`, replacing the final `out.ok = true;` block:

```cpp
    // Phase 3: reclaim what the repairs orphaned. Skipped on cancel (the pass is
    // incomplete and will be re-offered) and when there is nothing worth the
    // whole-file rewrite. Progress counters are reused for the compact bar.
    if (!out.cancelled) {
        const uint64_t wasted = v.wasted_bytes();
        if (wasted > 0) {
            phase_.store(MigrationPhase::Compacting);
            progress_.done.store(0);
            progress_.total.store(0);
            if (v.compact(&progress_) == vault::VaultResult::Ok) {
                out.reclaimed_bytes = wasted;
            }
            // A failed compact is NOT a failed migration: the repairs are
            // already committed and the vault is valid, just larger than ideal.
        }
    }

    out.ok = true;
    phase_.store(MigrationPhase::Done);
    outcome_ = std::move(out);
    done_.store(true);
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
scripts/test.sh 2>&1 | tail -20
```
Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/ui/migration_job.cpp tests/ui/test_migration_job.cpp
git commit -m "feat(ui): reclaim orphaned chunks after migration"
```

---

### Task 8: Unlock-time offer and progress modal

**Files:**
- Modify: `src/app/app.h`, `src/app/app.cpp` (post-unlock hook, modal state, poll + draw)
- Modify: `src/ui/settings_overlay.cpp` (manual trigger)
- Test: manual, via the `running-the-app` skill

**Interfaces:**
- Consumes: `vault::migration_pending`, `vault::scan_migration`, `media::PROBE_CAPS_GEN`, `ui::MigrationJob`, `ui::MigrationPhase`, `ui::MigrationOutcome`.
- Produces: no new API for later tasks.

This task is UI wiring with no unit-testable seam, so it has no TDD cycle. Read `src/ui/ui_spec` conventions (`mem:ui_spec`) and follow the existing modal patterns before writing anything — in particular how `FileOpJob`'s compact modal is polled and drawn, and how `consent_dialog` renders a default-cancel confirmation.

- [ ] **Step 1: Read the existing modal patterns**

```bash
rg -n "FileOpJob|progress_modal|consent_dialog" src/app/app.cpp src/ui/progress_modal.h | head -30
```
Understand how an in-flight job is polled each frame and how its outcome is collected exactly once. Mirror that; do not invent a second pattern.

- [ ] **Step 2: Add the post-unlock check**

Where `UnlockJob::take_outcome()` returns success in `src/app/app.cpp`, before the gallery screen is pushed:

```cpp
    // Phase 65: offer the one-time migration. Detection is a pure tree walk
    // (no I/O), so this costs nothing even on a huge vault.
    if (vault::migration_pending(vault::vault_settings(vault_), media::PROBE_CAPS_GEN)) {
        const vault::MigrationScan scan = vault::scan_migration(vault_);
        if (scan.empty()) {
            // Nothing to do: stamp and move on silently, so this vault is never
            // asked again.
            (void)vault::commit_migration(
                vault_, vault::stamp_migrated(vault::vault_settings(vault_),
                                              media::PROBE_CAPS_GEN));
        } else {
            pending_migration_ = scan;      // drives the offer modal
        }
    }
```

- [ ] **Step 3: Draw the offer modal**

A default-cancel confirmation stating the counts and bytes, with no time estimate. Wording:

> **Vault upgrade available**
> This vault has {videos} video(s) and {images} image(s) that were imported before this build could read them fully. Upgrading reads {bytes} and rewrites the vault once, then reclaims unused space.
> The app is unusable while this runs. You can cancel at any time.
> [ Upgrade now ]   [ Not now ]

"Not now" clears `pending_migration_` for the session and proceeds to the gallery. It is offered again at the next unlock.

- [ ] **Step 4: Draw the progress modal**

On "Upgrade now", call `migration_job_.start(vault_)` and draw a modal each frame while `active()`, labelled from `phase()`:

- `Scanning` → "Preparing…"
- `Repairing` → "Upgrading {done} / {total}"
- `Committing` → "Saving…"
- `Compacting` → "Reclaiming space…"

Nothing else may touch the vault while `active()` — no gallery listing, no thumbnail decrypt. That is the job's threading contract and violating it is a data race, not a glitch.

Collect with `take_outcome()` exactly once and show the summary: fixed counts, skipped count, and reclaimed bytes. On `!ok`, show `outcome.error`.

- [ ] **Step 5: Add the manual trigger**

In `src/ui/settings_overlay.cpp`, add a row that runs the same flow regardless of watermark state, so a user can re-run after a build with new codec support. Label it "Re-check vault for upgrades".

- [ ] **Step 6: Verify in the real app**

Use the `running-the-app` skill. Create a vault, import a GIF, force the pre-v7 state, relaunch, and confirm: the offer appears, "Not now" proceeds cleanly, "Upgrade now" shows progress and a summary, and a second unlock offers nothing.

- [ ] **Step 7: Commit**

```bash
git add src/app/app.h src/app/app.cpp src/ui/settings_overlay.cpp
git commit -m "feat(ui): unlock-time migration offer and progress modal"
```

---

### Task 9: Delete the lazy repair paths

**Files:**
- Delete: `src/ui/video_repair.h`, `src/ui/video_repair.cpp`, `src/ui/anim_repair.h`, `src/ui/anim_repair.cpp`
- Delete: any `tests/ui/test_video_repair.cpp` / `test_anim_repair.cpp` that exist
- Modify: `src/ui/gallery_grid.cpp` (:290), `src/ui/image_viewer.cpp` (:228), `src/ui/image_viewer.h` (:267)
- Modify: `src/vault/vault.h`, `src/vault/vault.cpp` — remove `repair_video_metadata` / `repair_image_animated` if nothing else calls them

**Interfaces:**
- Consumes: the migration must be fully working (Tasks 1–8) before this removal.
- Produces: nothing.

- [ ] **Step 1: Confirm every caller is gone**

```bash
rg -n "video_repair|anim_repair|AnimSniffGate|repair_unknown_video_metadata|maybe_repair_animated" src/ tests/
```
Expected after the edits below: no hits outside the files being deleted.

- [ ] **Step 2: Remove the call sites**

- `src/ui/gallery_grid.cpp:290` — delete the `ui::repair_unknown_video_metadata(...)` call and the `#include "ui/video_repair.h"`. Read the surrounding comment at :289 and delete it too; it describes behaviour that no longer exists.
- `src/ui/image_viewer.cpp:228` — delete the `maybe_repair_animated(...)` call and the include.
- `src/ui/image_viewer.h:267` — delete the `AnimSniffGate anim_sniff_gate_;` member and the comment above it, and the `#include "ui/anim_repair.h"`.

- [ ] **Step 3: Delete the modules**

```bash
git rm src/ui/video_repair.h src/ui/video_repair.cpp src/ui/anim_repair.h src/ui/anim_repair.cpp
```

- [ ] **Step 4: Decide the fate of the old Vault repair methods**

```bash
rg -n "repair_video_metadata|repair_image_animated" src/ tests/
```
If the only remaining hits are the declarations, definitions, and tests, remove `Vault::repair_video_metadata` and `Vault::repair_image_animated` along with their tests — `apply_video_probe` / `apply_image_animated` + `commit_migration` supersede them. Keep `VideoMeta::probe_failed_session` **only** if something still reads it; otherwise remove that field too, which requires no format change since it was never serialised. Verify with `rg -n "probe_failed_session" src/`.

- [ ] **Step 5: Regenerate and run everything**

```bash
scripts/gen.sh && scripts/test.sh 2>&1 | tail -20
```
Expected: all tests pass. Deleted tests are gone; nothing else should regress.

- [ ] **Step 6: Verify the vault no longer mutates while browsing**

This is the whole point of the phase, so verify it directly rather than assuming.

```bash
# With a migrated vault, note size and mtime, browse every gallery, then re-check.
stat -c '%s %Y' /path/to/test.osv
```
Use the `running-the-app` skill to browse all galleries and open several images and videos, then re-run `stat`. Expected: size and mtime unchanged.

- [ ] **Step 7: Commit**

```bash
git add -A src/ui src/vault tests
git commit -m "refactor(ui): delete lazy in-browse repair paths"
```

---

### Task 10: Transfer watermark lowering

The hole that Task 9 opens: a node moved in from an un-migrated vault lands in a vault whose watermark claims the work is done, and nothing lazy remains to fix it.

**Files:**
- Modify: `src/vault/transfer.cpp` (and `src/vault/transfer.h` if a helper is exported)
- Test: `tests/vault/test_migration.cpp` (append)

**Interfaces:**
- Consumes: `VaultSettings::migrated_index_version`, `VaultSettings::migrated_probe_caps`, `vault::set_vault_settings`.
- Produces: no new public API — the lowering is internal to transfer.

- [ ] **Step 1: Write the failing test**

```cpp
TEST(transfer_from_unmigrated_vault_lowers_destination_watermark)
{
    MigTempVault src_tv("xfer_src");
    MigTempVault dst_tv("xfer_dst");

    vault::Vault src;
    REQUIRE(vault::Vault::create(src_tv.str(), mig_bytes("pw"), {}, kMigKdf, src)
            == vault::VaultResult::Ok);
    REQUIRE(src.add_image("", mig_pattern(1000, 1), "a.png") == vault::VaultResult::Ok);
    // src stays un-migrated (watermark 0/0).
    CHECK(vault::migration_pending(vault::vault_settings(src), 1));

    vault::Vault dst;
    REQUIRE(vault::Vault::create(dst_tv.str(), mig_bytes("pw"), {}, kMigKdf, dst)
            == vault::VaultResult::Ok);
    REQUIRE(vault::commit_migration(
                dst, vault::stamp_migrated(vault::vault_settings(dst), 1))
            == vault::VaultResult::Ok);
    CHECK(!vault::migration_pending(vault::vault_settings(dst), 1));

    const std::vector<std::string> names{"a.png"};
    REQUIRE(vault::transfer_images(src, "", names, dst, "",
                                   vault::TransferMode::Copy) == vault::VaultResult::Ok);

    // The destination inherited un-backfilled content, so it owes a migration again.
    CHECK(vault::migration_pending(vault::vault_settings(dst), 1));
}
```

`transfer_images` is a placeholder for the real entry point — read `src/vault/transfer.h` and use the actual name and signature.

- [ ] **Step 2: Run the test to verify it fails**

```bash
scripts/test.sh 2>&1 | grep -E "lowers_destination_watermark|error:" | head -20
```
Expected: the final `CHECK` fails — the destination still reads as migrated.

- [ ] **Step 3: Lower the watermark on transfer-in**

In `src/vault/transfer.cpp`, at the point where a transfer has successfully added nodes to the destination and before its commit, add:

```cpp
// Phase 65: content from an un-migrated vault carries un-backfilled metadata
// (codec Unknown, animated flag never sniffed). With the lazy repair paths gone,
// nothing would ever fix it if the destination kept claiming to be migrated —
// so the destination inherits the source's lower watermark and re-offers the
// migration at its next unlock.
{
    const VaultSettings& s_src = vault_settings(src);
    VaultSettings        s_dst = vault_settings(dst);
    bool lowered = false;
    if (s_src.migrated_index_version < s_dst.migrated_index_version) {
        s_dst.migrated_index_version = s_src.migrated_index_version;
        lowered = true;
    }
    if (s_src.migrated_probe_caps < s_dst.migrated_probe_caps) {
        s_dst.migrated_probe_caps = s_src.migrated_probe_caps;
        lowered = true;
    }
    if (lowered) (void)set_vault_settings(dst, std::move(s_dst));
}
```

Apply this to every transfer entry point that moves media into a destination vault — images, whole galleries, the bulk gallery variant, and combine. Enumerate them from `src/vault/transfer.h` and make sure none is missed; a single missed path silently reintroduces the hole.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
scripts/test.sh 2>&1 | tail -20
```
Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/vault/transfer.cpp src/vault/transfer.h tests/vault/test_migration.cpp
git commit -m "fix(vault): inherit lower migration watermark on transfer-in"
```

---

### Task 11: Docs, ROADMAP, and Serena memories

**Files:**
- Modify: `ROADMAP.md` (phase index row + the format-history block at the tail)
- Modify: `docs/roadmap/phase-65-blocking-migration.md` (mark shipped)
- Modify: Serena memories

**Interfaces:**
- Consumes: the finished implementation.
- Produces: nothing.

- [ ] **Step 1: Add the ROADMAP row**

Append to the phase index table, matching the existing format exactly:

```
| 65 | Blocking vault migration | ✅ | One-time blocking, parallel, batch-committed upgrade pass offered at unlock, replacing the lazy in-browse repairs so a `.osv` stops mutating while browsing. Watermark in `VaultSettings` (`INDEX_VERSION = 10`). → [details](docs/roadmap/phase-65-blocking-migration.md) |
```

- [ ] **Step 2: Add the format-history entry**

In the block at the tail of `ROADMAP.md`, matching the Phase 47/49 entries' style:

```
> - **Phase 65 (Blocking migration):** ✅ shipped — a vault-global **migration
>   watermark** appended to the settings block (`migrated_index_version u8`,
>   `migrated_probe_caps u16`), bumping `INDEX_VERSION` to **10**; pre-v10 blobs
>   read `0/0` and are therefore treated as never migrated. A watermark claiming
>   a future index version is rejected on deserialise, not clamped. The lazy
>   in-browse repairs (`ui/video_repair.*`, `ui/anim_repair.*`) are removed.
```

- [ ] **Step 3: Update the Serena memories**

Per CLAUDE.md's change→memory table. Each is a real edit, not a rubber stamp:

- `mem:module/vault` — add `migration.*`; the watermark fields; `apply_video_probe` / `apply_image_animated` / `commit_migration`; the transfer watermark-lowering rule; removal of `repair_video_metadata` / `repair_image_animated` if Task 9 removed them.
- `mem:module/ui` — add `migration_job.*` and its threading contract; remove `video_repair.*` and `anim_repair.*`.
- `mem:vault_format` — `INDEX_VERSION 10` and the settings-block tail layout.
- `mem:ui_spec` — the unlock-time offer, its wording, and the four-label progress modal.
- `mem:core` — note the coordinator+pool model alongside the Phase 50 notes, and that a blocking job owns the vault exclusively.

- [ ] **Step 4: Audit the memory graph**

```bash
serena memories check
```
Expected: no stale references to the deleted modules.

- [ ] **Step 5: Full verification before the PR**

```bash
scripts/test.sh && scripts/test.sh --asan && scripts/test.sh --tsan
```
Expected: three clean runs. Do not open the PR until all three pass — claiming green without running them violates the project's completion rules.

- [ ] **Step 6: Commit and open the PR**

```bash
git add ROADMAP.md docs/roadmap/ .serena/memories/
git commit -m "docs: Phase 65 ROADMAP, format history, and memory updates"
git push -u origin phase-65-blocking-migration
gh pr create --title "Phase 65: blocking vault migration" --body "..."
```

- [ ] **Step 7: STOP**

⛔ Post the PR link, confirm CI is green and SonarCloud is clean, and **hand off to the owner**. Never run `gh pr merge`. Green gates are permission to ask, never to merge.

---

## Notes for the implementer

**Two things this plan could not verify statically** — resolve them before relying on them:

1. **`media::probe_video` re-entrancy.** Task 6 runs it on N threads at once. Each call builds its own `AVFormatContext` over its own `MemAvio`, which should be safe, but read `src/media/video_probe.cpp` and `src/media/mem_avio.cpp` and confirm there is no shared or static state. If there is, serialise just the probe call behind a mutex and keep the poster encode parallel — that preserves most of the win. TSAN in Task 6 Step 5 is the backstop.

2. **The exact transfer entry points.** Task 10 must cover every path that moves media into a destination vault. Enumerate them from `src/vault/transfer.h` rather than trusting the four this plan names.

3. **The "still undecodable" case has no direct test, deliberately.** The spec requires that such a video be skipped, that the watermark still advance, and that it not be retried next unlock. A synthetic undecodable video is not constructible through the public API — `add_video` probes on import and rejects what it cannot read, which is exactly the state the migration exists to repair and cannot be manufactured after the fact. The behaviour is covered indirectly: `apply_video_probe` returns `Ok` without writing when `probe.codec == Unknown` (Task 3), `apply_one` counts that as `videos_skipped` rather than `failed` (Task 6), and `migration_job_is_not_reoffered_after_a_full_pass` (Task 5) proves the watermark advances regardless. If a fixture of a genuinely unsupported container is ever added to `tests/media/fixtures/`, add the direct test then — do not fake one by stubbing the probe.

**Where the design is deliberately not defensive:** a crash mid-migration loses all migration work, by design. Nothing commits until the end, so the vault is exactly as it was and the pass simply re-runs. Do not add incremental commits to "improve" this — it was an explicit design decision, and adding them changes the fsync profile and the crash story the spec documents.
