#include "test_framework.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "platform/vault_registry.h"

namespace fs = std::filesystem;

// RAII unique temp file path, removed on destruction.
struct TempFile {
    fs::path path;
    explicit TempFile(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_reg_" + std::string(tag) + "_" + std::to_string(ctr++) + ".list");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempFile() { std::error_code ec; fs::remove(path, ec); }
};

static std::string read_text(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

TEST(registry_empty_when_no_file)
{
    TempFile tf("empty");
    platform::VaultRegistry reg(tf.path);
    CHECK_TRUE(reg.list().empty());
}

TEST(registry_add_then_list)
{
    TempFile tf("add");
    platform::VaultRegistry reg(tf.path);
    REQUIRE(reg.add("/vaults/a.osv"));
    auto v = reg.list();
    REQUIRE(v.size() == 1);
    CHECK_TRUE(v[0] == fs::path("/vaults/a.osv"));
}

TEST(registry_add_moves_to_front_and_dedups)
{
    TempFile tf("front");
    platform::VaultRegistry reg(tf.path);
    REQUIRE(reg.add("/vaults/a.osv"));
    REQUIRE(reg.add("/vaults/b.osv"));
    REQUIRE(reg.add("/vaults/a.osv"));   // re-add a: should move to front, not duplicate
    auto v = reg.list();
    REQUIRE(v.size() == 2);
    CHECK_TRUE(v[0] == fs::path("/vaults/a.osv"));
    CHECK_TRUE(v[1] == fs::path("/vaults/b.osv"));
}

TEST(registry_remove)
{
    TempFile tf("rm");
    platform::VaultRegistry reg(tf.path);
    REQUIRE(reg.add("/vaults/a.osv"));
    REQUIRE(reg.add("/vaults/b.osv"));
    REQUIRE(reg.remove("/vaults/a.osv"));
    auto v = reg.list();
    REQUIRE(v.size() == 1);
    CHECK_TRUE(v[0] == fs::path("/vaults/b.osv"));
}

TEST(registry_persists_across_instances)
{
    TempFile tf("persist");
    { platform::VaultRegistry reg(tf.path); REQUIRE(reg.add("/vaults/a.osv")); }
    platform::VaultRegistry reg2(tf.path);
    auto v = reg2.list();
    REQUIRE(v.size() == 1);
    CHECK_TRUE(v[0] == fs::path("/vaults/a.osv"));
}

TEST(registry_stores_no_secrets_only_paths)
{
    TempFile tf("nosecrets");
    platform::VaultRegistry reg(tf.path);
    REQUIRE(reg.add("/vaults/a.osv"));
    REQUIRE(reg.add("/vaults/b.osv"));
    // The raw file is exactly the two paths (most-recent first), one per line,
    // and nothing else — no password/key bytes could have been written.
    const std::string raw = read_text(tf.path);
    CHECK_EQ(raw, std::string("/vaults/b.osv\n/vaults/a.osv\n"));
}

// vaults.list is written '/'-separated on every platform.
//
// list() normalizes each line it reads (untrusted input), and lexically_normal()
// renders with the platform's preferred separator — so on Windows a line written
// as "/vaults/a.osv" reads back as "\vaults\a.osv". add()/remove() rewrite the
// whole file from list(), which would otherwise leave the file holding a mix of
// both separators: the just-added entry as the caller spelled it, and every
// older entry in backslash form. Pin the one stable format.
TEST(registry_file_is_written_generic_separated)
{
    TempFile tf("generic");
    platform::VaultRegistry reg(tf.path);

    // A NATIVE path, spelled the way the platform's file dialog hands it over.
    const fs::path native = fs::path("vaults") / "sub" / "a.osv";
    REQUIRE(reg.add(native));
    REQUIRE(reg.add("/vaults/b.osv"));   // forces the first entry through list()

    const std::string raw = read_text(tf.path);
    CHECK_TRUE(raw.find('\\') == std::string::npos);

    // And it still reads back as the same path it went in as.
    auto v = reg.list();
    REQUIRE(v.size() == 2);
    CHECK_TRUE(v[1] == native);
}

TEST(registry_seed_if_empty_adds_existing_candidate)
{
    TempFile tf("seed");
    // Create a real candidate file so exists() is true.
    TempFile cand("seedcand");
    { std::ofstream(cand.path) << "x"; }

    platform::VaultRegistry reg(tf.path);
    reg.seed_if_empty(cand.path);
    auto v = reg.list();
    REQUIRE(v.size() == 1);
    CHECK_TRUE(v[0] == cand.path);
}

TEST(registry_seed_if_empty_skips_missing_candidate)
{
    TempFile tf("seedmiss");
    platform::VaultRegistry reg(tf.path);
    reg.seed_if_empty("/no/such/vault.osv");
    CHECK_TRUE(reg.list().empty());
}

TEST(registry_seed_if_empty_noop_when_not_empty)
{
    TempFile tf("seednoop");
    TempFile cand("seednoopcand");
    { std::ofstream(cand.path) << "x"; }

    platform::VaultRegistry reg(tf.path);
    REQUIRE(reg.add("/vaults/a.osv"));
    reg.seed_if_empty(cand.path);     // already non-empty: must not add
    auto v = reg.list();
    REQUIRE(v.size() == 1);
    CHECK_TRUE(v[0] == fs::path("/vaults/a.osv"));
}

TEST(registry_empty_path_instance_is_safe_noop)
{
    platform::VaultRegistry reg;   // default ctor: no file
    CHECK_TRUE(reg.list().empty());
    CHECK_FALSE(reg.add("/vaults/a.osv"));   // nothing to write to
    CHECK_TRUE(reg.list().empty());
}
