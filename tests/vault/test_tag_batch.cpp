#include "test_framework.h"

#include <algorithm>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "vault/file_util.h"
#include "vault/vault.h"
#include "vault/commit_lane.h"

namespace fs = std::filesystem;

using vault::Vault;
using vault::VaultResult;

// --- helpers --------------------------------------------------------------

// Cheap Argon2 params so tests stay fast.
static const crypto::KdfParams kKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

// Internal linkage: several test files each define their own `TempVault`
// with a DIFFERENT layout. At namespace scope those are one-definition-rule
// violations — the member functions are implicitly inline, so the linker keeps
// a single copy and silently discards the rest.
namespace {

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_tagbatch_test_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempVault()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
    std::string str() const { return path.string(); }
};

}  // namespace

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

// --- tests ----------------------------------------------------------------

TEST(add_tag_batch_tags_everything_in_one_commit)
{
    TempVault tv("add");
    auto img = pattern(3000, 1);
    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == VaultResult::Ok);

    std::vector<std::string> paths;
    for (int i = 0; i < 50; ++i) {
        const std::string name = "img" + std::to_string(i) + ".jpg";
        REQUIRE(v.add_image("", img, name) == VaultResult::Ok);
        paths.push_back(name);
    }
    paths.push_back("missing.jpg");   // skipped, not an error

    vault::fileutil::sync_call_count().store(0);
    REQUIRE(vault::add_tag_batch(v, paths, "artist:bob") == VaultResult::Ok);
    const uint64_t syncs = vault::fileutil::sync_call_count().load();
    CHECK(syncs <= 3);   // ONE commit (the per-item path paid one commit each)

    for (const auto* c : v.list(""))
        CHECK(std::ranges::count_if(c->tags,
            [](const crypto::SecureString& t) { return t == "artist:bob"; }) == 1);

    // Idempotent second batch: nothing changes, NO commit.
    vault::fileutil::sync_call_count().store(0);
    REQUIRE(vault::add_tag_batch(v, paths, "ARTIST:BOB") == VaultResult::Ok);
    CHECK_EQ(vault::fileutil::sync_call_count().load(), 0u);
}

TEST(remove_tag_batch_removes_everywhere_and_persists)
{
    TempVault tv("rm");
    auto img = pattern(3000, 2);
    {
        Vault v;
        REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == VaultResult::Ok);
        REQUIRE(v.add_image("", img, "a.jpg") == VaultResult::Ok);
        REQUIRE(v.add_image("", img, "b.jpg") == VaultResult::Ok);
        const std::vector<std::string> paths{"a.jpg", "b.jpg"};
        REQUIRE(vault::add_tag_batch(v, paths, "x") == VaultResult::Ok);
        REQUIRE(vault::remove_tag_batch(v, paths, "X") == VaultResult::Ok);   // ci
        for (const auto* c : v.list("")) CHECK(c->tags.empty());
        v.lock();
    }
    Vault v2;
    REQUIRE(Vault::open(tv.str(), v2) == VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == VaultResult::Ok);
    for (const auto* c : v2.list("")) CHECK(c->tags.empty());
}

TEST(tag_batch_locked_and_invalid)
{
    TempVault tv("locked");
    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == VaultResult::Ok);
    const std::vector<std::string> paths{"a.jpg"};
    CHECK(vault::add_tag_batch(v, paths, "   ") == VaultResult::InvalidArg);
    CHECK(vault::remove_tag_batch(v, paths, "  ") == VaultResult::Ok);   // no-op
    v.lock();
    CHECK(vault::add_tag_batch(v, paths, "x") == VaultResult::Locked);
    CHECK(vault::remove_tag_batch(v, paths, "x") == VaultResult::Locked);
}

TEST(tag_batch_through_commit_lane_is_async_and_durable)
{
    TempVault tv("lane");
    auto img = pattern(3000, 4);
    std::vector<std::string> paths;
    {
        Vault v;
        REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == VaultResult::Ok);
        for (int i = 0; i < 10; ++i) {
            const std::string name = "img" + std::to_string(i) + ".jpg";
            REQUIRE(v.add_image("", img, name) == VaultResult::Ok);
            paths.push_back(name);
        }

        vault::CommitLane lane;
        lane.start(v);
        v.set_commit_router(&lane);

        REQUIRE(vault::add_tag_batch(v, paths, "artist:bob") == VaultResult::Ok);
        CHECK(lane.flush());          // async write completed, no failure
        v.lock();                     // auto-stops the lane before key wipe
    }
    Vault v2;
    REQUIRE(Vault::open(tv.str(), v2) == VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == VaultResult::Ok);
    for (const auto* c : v2.list(""))
        CHECK(std::ranges::count_if(c->tags,
            [](const crypto::SecureString& t) { return t == "artist:bob"; }) == 1);
}
