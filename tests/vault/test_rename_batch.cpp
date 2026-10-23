#include "crypto/random.h"
#include "test_framework.h"
#include "vault/fixtures.h"
#include "vault/rename_batch.h"
#include "vault/staging.h"

namespace {
struct RenameFixture {
    std::filesystem::path path = std::filesystem::temp_directory_path() / "osv-rename-batch.osv";
    vault::Vault v;
    RenameFixture()
    {
        std::filesystem::remove_all(path);
    }
    ~RenameFixture()
    {
        v.lock();
        std::filesystem::remove_all(path);
    }
};
}  // namespace
TEST(rename_batch_captures_extensions_unicode_and_unmatched)
{
    RenameFixture f;
    REQUIRE(vaulttest::make_directory_vault(f.v, f.path));
    REQUIRE(f.v.create_gallery("album") == vault::VaultResult::Ok);
    auto* root = f.v.resolve_node("");
    vault::IndexNode n;
    n.type = vault::IndexNode::Type::Image;
    n.name = "猫-123.jpg";
    REQUIRE(crypto::fill_random(n.node_id));
    root->children.push_back(std::move(n));
    vault::RenameOptions o;
    o.pattern = "^(.*)-([0-9]+)$";
    o.replacement = "$2-$1";
    const std::vector<vault::RenameId> ids{root->children[0].node_id, root->children[1].node_id};
    auto p = vault::preview_rename(f.v, ids, o);
    REQUIRE(p.valid());
    REQUIRE(p.rows.size() == 2);
    for (const auto& row : p.rows) {
        if (row.before == "album")
            CHECK(row.state == vault::RenameState::Unmatched);
        else
            CHECK(row.after == "123-猫.jpg");
    }
}
TEST(rename_batch_rejects_invalid_unsafe_collision_and_bounded_pattern)
{
    RenameFixture f;
    REQUIRE(vaulttest::make_directory_vault(f.v, f.path));
    REQUIRE(f.v.create_gallery("aaa") == vault::VaultResult::Ok);
    REQUIRE(f.v.create_gallery("bbb") == vault::VaultResult::Ok);
    const std::vector<vault::RenameId> ids{f.v.resolve_node("aaa")->node_id};
    vault::RenameOptions o;
    o.pattern = "(";
    CHECK(!vault::preview_rename(f.v, ids, o).valid());
    o.pattern = ".*";
    o.replacement = "../bad";
    CHECK(!vault::preview_rename(f.v, ids, o).valid());
    o.replacement = "bbb";
    CHECK(!vault::preview_rename(f.v, ids, o).valid());
    o.pattern = std::string(4097, 'a');
    CHECK(!vault::preview_rename(f.v, ids, o).valid());
    REQUIRE(f.v.create_gallery(std::string(200, 'a') + "!") == vault::VaultResult::Ok);
    const std::vector<vault::RenameId> evil{f.v.resolve_node(std::string(200, 'a') + "!")->node_id};
    o.pattern = "(*NO_START_OPT)(a+)+$";
    CHECK(!vault::preview_rename(f.v, evil, o).valid());
}
TEST(rename_batch_swap_stale_cancel_recursive_and_reopen)
{
    RenameFixture f;
    REQUIRE(vaulttest::make_directory_vault(f.v, f.path));
    REQUIRE(f.v.create_gallery("ab") == vault::VaultResult::Ok);
    REQUIRE(f.v.create_gallery("ba") == vault::VaultResult::Ok);
    REQUIRE(f.v.create_gallery("ab/ab") == vault::VaultResult::Ok);
    const auto id = f.v.resolve_node("ab")->node_id;
    const std::vector<vault::RenameId> ids{id, f.v.resolve_node("ba")->node_id};
    vault::RenameOptions o;
    o.pattern = "^(.)(.)$";
    o.replacement = "$2$1";
    o.recursive = true;
    auto p = vault::preview_rename(f.v, ids, o);
    REQUIRE(p.valid());
    REQUIRE(p.rows.size() == 3);
    vault::OpProgress progress;
    progress.cancel = true;
    CHECK(vault::apply_rename(f.v, p, &progress) != vault::VaultResult::Ok);
    CHECK(f.v.resolve_node("ab")->node_id == id);
    REQUIRE(vault::apply_rename(f.v, p) == vault::VaultResult::Ok);
    CHECK(f.v.resolve_node("ba")->node_id == id);
    CHECK(f.v.resolve_node("ba/ba") != nullptr);
    CHECK(vault::apply_rename(f.v, p) != vault::VaultResult::Ok);
    f.v.lock();
    REQUIRE(vaulttest::open_vault(f.path, f.v));
    CHECK(f.v.resolve_node("ba")->node_id == id);
}

TEST(rename_batch_case_only_first_all_named_captures_and_invalid_capture)
{
    RenameFixture f;
    REQUIRE(vaulttest::make_directory_vault(f.v, f.path));
    REQUIRE(f.v.create_gallery("a-a") == vault::VaultResult::Ok);
    const std::vector<vault::RenameId> ids{f.v.resolve_node("a-a")->node_id};
    vault::RenameOptions o;
    o.pattern = "(?<letter>a)";
    o.replacement = "${letter}Z";
    auto p = vault::preview_rename(f.v, ids, o);
    REQUIRE(p.valid());
    CHECK(p.rows[0].after == "aZ-a");
    o.all_matches = true;
    p = vault::preview_rename(f.v, ids, o);
    REQUIRE(p.valid());
    CHECK(p.rows[0].after == "aZ-aZ");
    o.replacement = "$99";
    CHECK(!vault::preview_rename(f.v, ids, o).valid());
    o.replacement = "A";
    p = vault::preview_rename(f.v, ids, o);
    REQUIRE(vault::apply_rename(f.v, p) == vault::VaultResult::Ok);
    CHECK(f.v.resolve_node("A-A") != nullptr);
}

#include "vault/file_util.h"
TEST(rename_batch_failed_commit_rolls_back_and_legacy_readonly_rejected)
{
    RenameFixture f;
    REQUIRE(vaulttest::make_legacy_writable_vault(f.v, f.path));
    REQUIRE(f.v.create_gallery("one") == vault::VaultResult::Ok);
    const std::vector<vault::RenameId> ids{f.v.resolve_node("one")->node_id};
    vault::RenameOptions o;
    o.pattern = "one";
    o.replacement = "two";
    auto p = vault::preview_rename(f.v, ids, o);
    REQUIRE(p.valid());
    vault::fileutil::inject_sync_failure(0);
    const auto result = vault::apply_rename(f.v, p);
    vault::fileutil::clear_sync_failure();
    CHECK(result == vault::VaultResult::IoError);
    CHECK(f.v.resolve_node("one") != nullptr);
    CHECK(f.v.resolve_node("two") == nullptr);
    f.v.lock();
    REQUIRE(vaulttest::open_vault(f.path, f.v));
    CHECK(f.v.resolve_node("one") != nullptr);
    CHECK(!vault::preview_rename(f.v, ids, o).valid());
    CHECK(vault::apply_rename(f.v, p) != vault::VaultResult::Ok);
}

#include <fstream>
#include <map>
namespace {
auto encrypted_objects(const std::filesystem::path& vault)
{
    std::map<std::string, std::string> result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(vault / "objects")) {
        if (!entry.is_regular_file()) continue;
        std::ifstream file(entry.path(), std::ios::binary);
        result.emplace(entry.path().string(),
                       std::string(std::istreambuf_iterator<char>(file), {}));
    }
    return result;
}
}  // namespace
TEST(rename_batch_cycle_keeps_original_ciphertext_and_ids)
{
    RenameFixture f;
    REQUIRE(vaulttest::make_directory_vault(f.v, f.path));
    const std::vector<uint8_t> data(128, 42);
    for (const char* name : {"abc.jpg", "bca.jpg", "cab.jpg"})
        REQUIRE(f.v.add_image("", data, name) == vault::VaultResult::Ok);
    const auto id = f.v.resolve_node("abc.jpg")->node_id;
    const std::vector<vault::RenameId> ids{id, f.v.resolve_node("bca.jpg")->node_id,
                                           f.v.resolve_node("cab.jpg")->node_id};
    const auto before = encrypted_objects(f.path);
    REQUIRE(!before.empty());
    vault::RenameOptions o;
    o.pattern = "^(.)(.)(.)$";
    o.replacement = "$2$3$1";
    auto p = vault::preview_rename(f.v, ids, o);
    REQUIRE(p.valid());
    REQUIRE(vault::apply_rename(f.v, p) == vault::VaultResult::Ok);
    CHECK(f.v.resolve_node("bca.jpg")->node_id == id);
    CHECK(encrypted_objects(f.path) == before);
    f.v.lock();
    REQUIRE(vaulttest::open_vault(f.path, f.v));
    crypto::SecureBytes read;
    REQUIRE(f.v.read_image(*f.v.resolve_node("bca.jpg"), read) == vault::VaultResult::Ok);
    CHECK(read.size() == data.size());
    CHECK(std::equal(data.begin(), data.end(), read.data()));
}

TEST(rename_batch_case_distinct_siblings_and_revalidate_new_collision)
{
    RenameFixture f;
    REQUIRE(vaulttest::make_directory_vault(f.v, f.path));
    REQUIRE(f.v.create_gallery("one") == vault::VaultResult::Ok);
    REQUIRE(f.v.create_gallery("One") == vault::VaultResult::Ok);
    const std::vector<vault::RenameId> ids{f.v.resolve_node("one")->node_id};
    vault::RenameOptions o;
    o.pattern = "one";
    o.replacement = "One";
    CHECK(!vault::preview_rename(f.v, ids, o).valid());
    o.replacement = "two";
    auto p = vault::preview_rename(f.v, ids, o);
    REQUIRE(p.valid());
    REQUIRE(f.v.create_gallery("two") == vault::VaultResult::Ok);
    CHECK(vault::apply_rename(f.v, p) != vault::VaultResult::Ok);
    CHECK(f.v.resolve_node("one") != nullptr);
}
