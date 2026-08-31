#include "test_framework.h"

// Phase 98 (security audit Phase E / OSV-AUD-005): atomic, link-safe export
// creation. create_new_file_within must atomically claim a brand-new file
// strictly inside the given directory — never following a symlink, never
// truncating an existing entry, and never relying on a check-then-open race.
//
// The tests exercise: basic create + write-through-handle, collision suffixing,
// unsafe-component rejection, symlink-at-candidate (Linux real symlinks), the
// injected attacker-race seam, concurrent creators, and failure cases. Symlink
// tests are POSIX-only for two reasons: creating a symlink on Windows needs
// SeCreateSymbolicLinkPrivilege (absent by default in CI), and the SUFFIX
// semantics are identical on Windows where the atomic create already refuses a
// reparse point at the candidate (CREATE_NEW -> ERROR_FILE_EXISTS).

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "platform/atomic_file.h"
#include "platform/path_utf8.h"

namespace fs = std::filesystem;

// RAII unique temp directory (sequence counter as in test_export.cpp).
struct TempDir {
    fs::path path;
    explicit TempDir(const char* tag)
    {
        static unsigned ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_atomic_" + std::string(tag) + "_" + std::to_string(ctr++));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;
};

namespace {

std::vector<uint8_t> read_all(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

}  // namespace

// --- Basic creation + write-through-handle ---------------------------------

TEST(atomic_create_makes_writable_file_and_reports_path)
{
    TempDir d("basic");
    auto f = platform::create_new_file_within(d.path, "cat.png");
    REQUIRE(f.has_value());
    REQUIRE(f->fp != nullptr);
    CHECK_TRUE(f->display_path == d.path / "cat.png");

    const char* payload = "hello export";
    const size_t n = std::strlen(payload);
    bool ok = std::fwrite(payload, 1, n, f->fp) == n;
    ok = std::fflush(f->fp) == 0 && ok;
    f.reset();  // RAII closes

    CHECK_TRUE(ok);
    CHECK_BYTES_EQ(std::span<const uint8_t>(read_all(d.path / "cat.png")),
                   std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(payload), n));
}

// --- Collision suffixing (Phase 10 naming, now atomic) ---------------------

TEST(atomic_create_suffixes_a_collision_without_overwriting)
{
    TempDir d("coll");
    {
        std::ofstream f(d.path / "a.png", std::ios::binary);
        const char* existing = "DO NOT OVERWRITE";
        f.write(existing, static_cast<std::streamsize>(std::strlen(existing)));
    }
    auto f = platform::create_new_file_within(d.path, "a.png");
    REQUIRE(f.has_value());
    CHECK_TRUE(f->display_path == d.path / "a (1).png");

    const char* payload = "the new one";
    std::fwrite(payload, 1, std::strlen(payload), f->fp);
    std::fflush(f->fp);
    f.reset();

    // The pre-existing file keeps its bytes verbatim.
    const auto old_bytes = read_all(d.path / "a.png");
    CHECK_BYTES_EQ(std::span<const uint8_t>(old_bytes),
                   std::span<const uint8_t>(reinterpret_cast<const uint8_t*>("DO NOT OVERWRITE"),
                                            std::strlen("DO NOT OVERWRITE")));
    CHECK_BYTES_EQ(std::span<const uint8_t>(read_all(d.path / "a (1).png")),
                   std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(payload),
                                            std::strlen(payload)));
}

TEST(atomic_create_skips_multiple_collisions)
{
    TempDir d("multi");
    for (int i = 0; i < 4; ++i) {
        std::ofstream f(i == 0 ? d.path / "a.png" : d.path / std::format("a ({}){}", i, ".png"),
                        std::ios::binary);
        f << "x";
    }
    auto f = platform::create_new_file_within(d.path, "a.png");
    REQUIRE(f.has_value());
    CHECK_TRUE(f->display_path == d.path / "a (4).png");  // 1..3 taken, 4 is free
}

TEST(atomic_create_suffixes_name_without_extension)
{
    TempDir d("noext");
    {
        std::ofstream f(d.path / "README", std::ios::binary);
        f << "x";
    }
    auto f = platform::create_new_file_within(d.path, "README");
    REQUIRE(f.has_value());
    CHECK_TRUE(f->display_path == d.path / "README (1)");
}

// --- Unsafe components are rejected up front --------------------------------

TEST(atomic_create_rejects_unsafe_components)
{
    TempDir d("unsafe");
    CHECK_FALSE(platform::create_new_file_within(d.path, "").has_value());
    CHECK_FALSE(platform::create_new_file_within(d.path, ".").has_value());
    CHECK_FALSE(platform::create_new_file_within(d.path, "..").has_value());
    CHECK_FALSE(platform::create_new_file_within(d.path, "a/b.png").has_value());
    CHECK_FALSE(platform::create_new_file_within(d.path, "a\\b.png").has_value());
    CHECK_FALSE(platform::create_new_file_within(d.path, "/etc/passwd").has_value());
    CHECK_FALSE(platform::create_new_file_within(d.path, std::string(256, 'x')).has_value());
    // Nothing was created.
    int files = 0;
    for (std::error_code ec; auto& e : fs::directory_iterator(d.path, ec)) { (void)e; ++files; }
    CHECK_EQ(files, 0);
}

// --- Symlink at the candidate is never followed (POSIX) --------------------

#if !defined(_WIN32)
TEST(atomic_create_does_not_follow_a_symlink_at_the_candidate)
{
    TempDir d("symlink");
    TempDir victim("symlink_victim");
    const fs::path target = victim.path / "redirected.bin";

    // The natural candidate is a symlink POINTING OUTSIDE the destination.
    std::error_code ec;
    fs::create_symlink(target, d.path / "a.png", ec);
    REQUIRE(!ec);

    auto f = platform::create_new_file_within(d.path, "a.png");
    REQUIRE(f.has_value());
    // Never follows the link: the name moves to a suffixed free component.
    CHECK_TRUE(f->display_path == d.path / "a (1).png");
    const char* payload = "stayed inside";
    std::fwrite(payload, 1, std::strlen(payload), f->fp);
    std::fflush(f->fp);
    f.reset();

    // The symlink target (outside the destination) was NEVER created/truncated.
    CHECK_FALSE(fs::exists(target));
}

TEST(atomic_create_rejects_a_dangling_symlink_candidate_without_unlinking)
{
    TempDir d("dangling");
    const fs::path ghost = d.path / "ghost.bin";  // does not exist anywhere
    std::error_code ec;
    fs::create_symlink(ghost, d.path / "a.png", ec);
    REQUIRE(!ec);

    // openat(O_EXCL) refuses the name even though the link target is absent:
    // the ENTRY "a.png" exists, so O_EXCL fails -> suffix, no follow, no unlink.
    auto f = platform::create_new_file_within(d.path, "a.png");
    REQUIRE(f.has_value());
    CHECK_TRUE(f->display_path == d.path / "a (1).png");
    CHECK_TRUE(fs::is_symlink(d.path / "a.png", ec));
    CHECK_FALSE(fs::exists(ghost));
}

TEST(atomic_create_refuses_a_destination_that_is_a_file)
{
    TempDir d("notdir");
    const fs::path file = d.path / "i_am_a_file";
    { std::ofstream f(file); f << "x"; }
    // The "directory" is actually a regular file: open(O_DIRECTORY) must fail.
    CHECK_FALSE(platform::create_new_file_within(file, "x.png").has_value());
}
#endif

// --- Injected attacker race (deterministic seam) ----------------------------

TEST(atomic_create_survives_an_attacker_creating_between_attempts)
{
    TempDir d("race");
    // Arm the seam: the first create attempt "sees" the name already taken even
    // though nothing was created, exactly like an attacker winning the window
    // between probe and open. The helper must move on to a suffix WITHOUT
    // truncating anything (there is nothing there yet) and still succeed.
    platform::inject_atomic_create_collision();
    auto f = platform::create_new_file_within(d.path, "a.png");
    REQUIRE(f.has_value());
    CHECK_TRUE(f->display_path == d.path / "a (1).png");
    const char* payload = "race survived";
    std::fwrite(payload, 1, std::strlen(payload), f->fp);
    std::fflush(f->fp);
    f.reset();
    CHECK_BYTES_EQ(std::span<const uint8_t>(read_all(d.path / "a (1).png")),
                   std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(payload),
                                            std::strlen(payload)));
    // No plain "a.png" was left behind by the seam.
    CHECK_FALSE(fs::exists(d.path / "a.png"));
}

// --- Concurrent creators get distinct names (no last-writer-wins) -----------

TEST(atomic_create_concurrent_creators_get_distinct_names)
{
    TempDir d("conc");
    constexpr int kThreads = 8;
    std::vector<std::optional<platform::NewOutputFile>> files(kThreads);
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&, i] {
            files[static_cast<size_t>(i)] =
                platform::create_new_file_within(d.path, "same.png");
        });
    }
    for (auto& t : threads) t.join();

    int ok_count = 0;
    std::vector<std::string> names;
    for (auto& f : files) {
        if (f.has_value() && f->fp) {
            ++ok_count;
            names.push_back(platform::path_to_utf8(f->display_path.filename()));
        }
    }
    // Every concurrent creator succeeded with a DISTINCT name — the atomic
    // exclusive create is the collision arbiter; no two got "same.png".
    CHECK_EQ(ok_count, kThreads);
    CHECK_EQ(names.size(), kThreads);
    for (const auto& a : names)
        for (const auto& b : names)
            if (&a != &b) CHECK_FALSE(a == b);
    // And "same.png" itself is exactly one of them (the unsuffixed winner).
    CHECK_EQ(static_cast<int>(std::count_if(names.begin(), names.end(),
                                            [](const std::string& s) { return s == "same.png"; })),
             1);
}

// --- Failure cases -----------------------------------------------------------

TEST(atomic_create_missing_directory_returns_nullopt)
{
    TempDir d("missing");
    const fs::path gone = d.path / "does" / "not" / "exist";
    CHECK_FALSE(platform::create_new_file_within(gone, "a.png").has_value());
}

TEST(atomic_create_does_not_leak_an_open_handle_on_write_of_small_file)
{
    // Regression guard: the NewOutputFile RAII must close the handle even when
    // the caller never writes and just drops it.
    TempDir d("raii");
    {
        auto f = platform::create_new_file_within(d.path, "a.png");
        REQUIRE(f.has_value());
    }  // destructor closes fp
    // Re-opening for write must not fail with a share violation (Windows) —
    // the previous handle was released.
    std::FILE* fp = platform::fopen_path(d.path / "a.png", "rb");
    REQUIRE(fp != nullptr);
    std::fclose(fp);
}