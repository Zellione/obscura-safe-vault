#include "test_framework.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "ui/dup_scan.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

static const crypto::KdfParams kFastKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

static std::vector<uint8_t> read_file(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

namespace {
struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_dsc_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempVault()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
    [[nodiscard]] std::string str() const { return path.string(); }
};
} // namespace

TEST(dup_scan_items_cover_whole_tree)
{
    TempVault tv("collect");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("a/b") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("",    pattern(100, 1), "root.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("a",   pattern(200, 2), "mid.png")  == vault::VaultResult::Ok);
    REQUIRE(v.add_image("a/b", pattern(300, 3), "leaf.png") == vault::VaultResult::Ok);

    auto items = ui::collect_scan_items(v);
    REQUIRE(items.size() == 3);
    auto find = [&](const std::string& p) {
        return std::ranges::find_if(items, [&](const ui::DupScanItem& it) {
            return it.node_path == p;
        });
    };
    REQUIRE(find("root.png") != items.end());
    REQUIRE(find("a/mid.png") != items.end());
    REQUIRE(find("a/b/leaf.png") != items.end());

    const auto& leaf = *find("a/b/leaf.png");
    CHECK_EQ(leaf.name, std::string("leaf.png"));
    CHECK_EQ(leaf.parent_path, std::string("a/b"));
    CHECK(!leaf.is_video);
    CHECK_EQ(leaf.bytes, uint64_t{300});
    REQUIRE(leaf.data_spans.size() == 1);
    CHECK(leaf.data_spans[0].second > 0);   // on-disk chunk length recorded
}

// --- DupScanJob worker tests ---

static ui::DupScanOutcome run_scan(const vault::Vault& v, bool perceptual)
{
    ui::DupScanJob job;
    job.start(v, ui::collect_scan_items(v), perceptual);
    while (job.active()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto out = job.take_outcome();
    if (!out.has_value()) throw std::runtime_error("take_outcome() returned nullopt");
    return *out;
}

TEST(dup_scan_finds_exact_duplicates_across_galleries)
{
    TempVault tv("exact");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);
    const auto dup    = pattern(4096, 7);
    const auto other  = pattern(4096, 9);   // same SIZE, different bytes
    const auto unique = pattern(1234, 5);
    REQUIRE(v.add_image("",  dup,    "one.png")   == vault::VaultResult::Ok);
    REQUIRE(v.add_image("g", dup,    "two.png")   == vault::VaultResult::Ok);
    REQUIRE(v.add_image("g", other,  "decoy.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("",  unique, "solo.png")  == vault::VaultResult::Ok);

    const auto out = run_scan(v, /*perceptual=*/false);
    CHECK(!out.cancelled);
    REQUIRE(out.groups.size() == 1);           // size-collided decoy must NOT group
    const auto& g = out.groups[0];
    CHECK(g.kind == ui::DupGroup::Kind::Identical);
    REQUIRE(g.members.size() == 2);
    std::vector<std::string> paths{g.members[0].node_path, g.members[1].node_path};
    std::ranges::sort(paths);
    CHECK_EQ(paths[0], std::string("g/two.png"));
    CHECK_EQ(paths[1], std::string("one.png"));
}

TEST(dup_scan_no_duplicates_yields_empty)
{
    TempVault tv("none");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(100, 1), "a.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(200, 2), "b.png") == vault::VaultResult::Ok);
    const auto out = run_scan(v, false);
    CHECK(out.groups.empty());
    CHECK_EQ(out.skipped, size_t{0});
}

// Perceptual pass end-to-end: the same decodable WebP twice, once with a
// trailing junk byte appended. Different byte size -> NOT an exact dupe; the
// decoder ignores trailing garbage -> identical pixels -> identical dHash ->
// one Similar group at distance 0.
TEST(dup_scan_perceptual_groups_reencoded_copy)
{
    TempVault tv("percep");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
            == vault::VaultResult::Ok);
    const auto webp = read_file(fs::path(OSV_FIXTURE_DIR) / "sample.webp");
    REQUIRE(!webp.empty());
    auto padded = webp;
    padded.push_back(0x00);
    REQUIRE(v.add_image("", webp,   "orig.webp") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", padded, "copy.webp") == vault::VaultResult::Ok);

    const auto out = run_scan(v, /*perceptual=*/true);
    REQUIRE(out.groups.size() == 1);
    CHECK(out.groups[0].kind == ui::DupGroup::Kind::Similar);
    CHECK_EQ(out.groups[0].distance_bits, 0);
    CHECK_EQ(out.groups[0].members.size(), size_t{2});
}

TEST(dup_scan_cancel_stops_and_reports_cancelled)
{
    TempVault tv("cancel");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
            == vault::VaultResult::Ok);
    const auto dup = pattern(1 << 16, 3);
    for (int i = 0; i < 20; ++i)
        REQUIRE(v.add_image("", dup, "img" + std::to_string(i) + ".png")
                == vault::VaultResult::Ok);
    ui::DupScanJob job;
    job.start(v, ui::collect_scan_items(v), false);
    job.cancel();
    while (job.active()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto out = job.take_outcome();
    REQUIRE(out.has_value());
    CHECK(out->cancelled);
}

TEST(dup_scan_vault_locked_under_worker_reports_cancelled)
{
    TempVault tv("locked");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
            == vault::VaultResult::Ok);
    const auto dup = pattern(4096, 7);
    REQUIRE(v.add_image("", dup, "one.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", dup, "two.png") == vault::VaultResult::Ok);

    // Collect items while vault is unlocked.
    auto items = ui::collect_scan_items(v);
    REQUIRE(items.size() == 2);

    // Lock the vault BEFORE starting the worker.
    v.lock();

    // Start scan with locked vault; worker should detect Locked on first read attempt
    // and set cancelled=true immediately.
    ui::DupScanJob job;
    job.start(v, std::move(items), false);
    while (job.active()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto out = job.take_outcome();
    REQUIRE(out.has_value());
    CHECK(out->cancelled);
    // No groups should be formed; all work stopped on vault lock.
    CHECK(out->groups.empty());
}

// Flip one byte at `pos` in the file at `path` (at-rest tamper, same helper as
// tests/vault/test_vault_integration.cpp).
static bool flip_byte(const std::string& path, long pos)
{
    std::FILE* fp = std::fopen(path.c_str(), "r+b");
    if (!fp) return false;
    bool ok = std::fseek(fp, pos, SEEK_SET) == 0;
    int c = ok ? std::fgetc(fp) : EOF;
    ok = ok && c != EOF && std::fseek(fp, pos, SEEK_SET) == 0 &&
         std::fputc(c ^ 0x01, fp) != EOF;
    std::fclose(fp);
    return ok;
}

// A skipped (unreadable) thumbnail must not shift the mapping between hashes
// and items: with a.webp / b.webp / c.webp all visually identical and b's
// stored thumbnail tampered, the Similar group must contain a and c — not b.
TEST(dup_scan_perceptual_skip_keeps_member_mapping)
{
    TempVault tv("skipmap");
    const auto webp = read_file(fs::path(OSV_FIXTURE_DIR) / "sample.webp");
    REQUIRE(!webp.empty());
    auto padded1 = webp; padded1.push_back(0x00);
    auto padded2 = webp; padded2.push_back(0x00); padded2.push_back(0x00);

    uint64_t b_thumb_offset = 0;
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kFastKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", webp,    "a.webp") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", padded1, "b.webp") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", padded2, "c.webp") == vault::VaultResult::Ok);
        for (const auto* n : v.list("")) {
            if (n->name == "b.webp") b_thumb_offset = n->meta.thumb_offset;
        }
        REQUIRE(b_thumb_offset != 0);
    }
    // Corrupt b's stored thumbnail ciphertext (past the 24-byte nonce).
    REQUIRE(flip_byte(tv.str(), static_cast<long>(b_thumb_offset) + 30));

    vault::Vault v;
    REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
    REQUIRE(v.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    const auto out = run_scan(v, /*perceptual=*/true);
    CHECK_EQ(out.skipped, size_t{1});
    REQUIRE(out.groups.size() == 1);
    REQUIRE(out.groups[0].members.size() == size_t{2});
    std::vector<std::string> names;
    for (const auto& m : out.groups[0].members) names.push_back(m.name);
    std::ranges::sort(names);
    CHECK_EQ(names[0], std::string("a.webp"));
    CHECK_EQ(names[1], std::string("c.webp"));
}
