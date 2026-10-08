#include "test_framework.h"

#include "vault/v3_db.h"
#include "vault/v3_object_store.h"
#include "vault/v3_recovery.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <unistd.h>

namespace {
namespace fs = std::filesystem;
using namespace vault::v3;

constexpr std::array<uint8_t, 32> KEY{1, 2, 3, 4, 5, 6, 7, 8};
constexpr Id ROOT{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
constexpr Id NODE{2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};

struct TempDir {
    fs::path path;
    TempDir()
    {
        std::array<char, 64> pattern{};
        std::snprintf(pattern.data(), pattern.size(), "/tmp/osv-v3-recovery-XXXXXX");
        if (const char* made = ::mkdtemp(pattern.data())) path = made;
    }
    ~TempDir()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

ObjectRecord object(uint8_t first)
{
    ObjectRecord result;
    result.object_id[0] = first;
    result.object_id[15] = first;
    result.node_id = NODE;
    result.encrypted_length = 512;
    result.plaintext_length = 17;
    result.frame_plain_limit = 1024;
    result.frame_count = 1;
    return result;
}

std::optional<Database> make_database(const fs::path& path, uint64_t original_size = 17)
{
    auto opened = Database::create(path, KEY, ROOT);
    if (!opened.database) return std::nullopt;
    NodeRecord node;
    node.node_id = NODE;
    node.parent_id = ROOT;
    node.type = NodeType::Image;
    node.display_name = "image.jpg";
    node.media_format = 1;
    node.original_size = original_size;
    if (opened.database->insert_node(node) != DbStatus::Ok) return std::nullopt;
    return std::move(*opened.database);
}
}  // namespace

TEST(v3_db_object_mutations_commit_reference_and_generation_together)
{
    TempDir temp;
    auto made = make_database(temp.path / "db");
    REQUIRE(made.has_value());
    auto db = std::move(*made);
    auto first = object(3);
    REQUIRE(db.commit_object_create(first) == DbStatus::Ok);
    auto settings = db.settings();
    REQUIRE(settings.status == DbStatus::Ok);
    CHECK_EQ(settings.value.app_generation, 1);

    auto second = object(4);
    auto replaced = db.commit_object_replace(second);
    REQUIRE(replaced.status == DbStatus::Ok);
    REQUIRE(replaced.value.has_value());
    CHECK_EQ(*replaced.value, first.object_id);
    CHECK_TRUE(db.object_is_referenced(second.object_id).value);
    CHECK_FALSE(db.object_is_referenced(first.object_id).value);
    CHECK_EQ(db.settings().value.app_generation, 2);

    auto removed = db.commit_object_delete(NODE, ObjectRole::OriginalImage);
    REQUIRE(removed.status == DbStatus::Ok);
    REQUIRE(removed.value.has_value());
    CHECK_EQ(*removed.value, second.object_id);
    CHECK_EQ(db.settings().value.app_generation, 3);
}

TEST(v3_gc_rechecks_reference_after_scan_and_preserves_newly_live_object)
{
    TempDir temp;
    auto root = VaultRoot::create(temp.path / "vault.osv");
    REQUIRE(root.has_value());
    auto lock = root->try_writer_lock();
    REQUIRE(lock.has_value());
    auto made = make_database(temp.path / "db");
    REQUIRE(made.has_value());
    auto db = std::move(*made);

    const auto stale = object(7);
    auto staged = root->create_staging_file();
    REQUIRE(staged.has_value());
    REQUIRE(staged->write_all(std::array<uint8_t, 4>{1, 2, 3, 4}));
    REQUIRE(staged->sync());
    REQUIRE(root->publish(*staged, stale.object_id));

    auto scan = reconcile_objects(*root, db);
    REQUIRE(scan.status == RecoveryStatus::Ok);
    REQUIRE(scan.garbage.size() == 1);
    REQUIRE(db.commit_object_create(stale) == DbStatus::Ok);

    const auto collected = collect_garbage(*root, *lock, db, scan.garbage, 0);
    CHECK_TRUE(collected.status == RecoveryStatus::Ok);
    CHECK_EQ(collected.deleted, 0);
    CHECK_TRUE(root->open_object(stale.object_id).has_value());
}

TEST(v3_reconciliation_distinguishes_missing_reference_from_garbage)
{
    TempDir temp;
    auto root = VaultRoot::create(temp.path / "vault.osv");
    REQUIRE(root.has_value());
    auto lock = root->try_writer_lock();
    REQUIRE(lock.has_value());
    auto made = make_database(temp.path / "db");
    REQUIRE(made.has_value());
    auto db = std::move(*made);
    const auto missing = object(9);
    REQUIRE(db.commit_object_create(missing) == DbStatus::Ok);

    auto scan = reconcile_objects(*root, db);
    REQUIRE(scan.status == RecoveryStatus::Ok);
    REQUIRE(scan.missing.size() == 1);
    CHECK_EQ(scan.missing[0], missing.object_id);
    CHECK_TRUE(scan.garbage.empty());
}

TEST(v3_deep_verify_authenticates_referenced_objects_and_classifies_garbage)
{
    TempDir temp;
    auto root = VaultRoot::create(temp.path / "vault.osv");
    REQUIRE(root.has_value());
    auto made = make_database(temp.path / "db", 5);
    REQUIRE(made.has_value());
    auto db = std::move(*made);

    ObjectWriteRequest request{.vault_id = ROOT,
                               .owner_node_id = NODE,
                               .role = ObjectRole::OriginalImage,
                               .media_format = 1};
    const std::array<uint8_t, 5> plain{1, 2, 3, 4, 5};
    const auto written = write_object(*root, KEY, request, plain);
    REQUIRE(written.status == ObjectStatus::Ok);
    ObjectRecord reference{written.info.object_id,        NODE,
                           ObjectRole::OriginalImage,     written.info.encrypted_length,
                           written.info.plaintext_length, written.info.frame_plain_limit,
                           written.info.frame_count,      0};
    REQUIRE(db.commit_object_create(reference) == DbStatus::Ok);

    auto garbage = root->create_staging_file();
    REQUIRE(garbage.has_value());
    REQUIRE(garbage->write_all(std::array<uint8_t, 3>{9, 8, 7}));
    REQUIRE(garbage->sync());
    ObjectId garbage_id{7};
    garbage_id[15] = 7;
    REQUIRE(root->publish(*garbage, garbage_id));

    const auto report = verify_vault(*root, db, KEY, ROOT, VerifyDepth::Deep);
    CHECK(report.status == RecoveryStatus::Ok);
    CHECK_EQ(report.objects_checked, 1);
    CHECK_EQ(report.authenticated_bytes, plain.size());
    CHECK_EQ(report.garbage_objects, 1);
    CHECK_FALSE(report.has_corruption());
}

TEST(v3_deep_verify_reports_unauthentic_referenced_object_without_deleting_it)
{
    TempDir temp;
    auto root = VaultRoot::create(temp.path / "vault.osv");
    REQUIRE(root.has_value());
    auto made = make_database(temp.path / "db", 4);
    REQUIRE(made.has_value());
    auto db = std::move(*made);

    ObjectWriteRequest request{.vault_id = ROOT,
                               .owner_node_id = NODE,
                               .role = ObjectRole::OriginalImage,
                               .media_format = 1};
    const std::array<uint8_t, 4> plain{1, 2, 3, 4};
    const auto written = write_object(*root, KEY, request, plain);
    REQUIRE(written.status == ObjectStatus::Ok);
    auto reference = ObjectRecord{written.info.object_id,        NODE,
                                  ObjectRole::OriginalImage,     written.info.encrypted_length,
                                  written.info.plaintext_length, written.info.frame_plain_limit,
                                  written.info.frame_count,      0};
    REQUIRE(db.commit_object_create(reference) == DbStatus::Ok);

    auto file = root->open_object(reference.object_id);
    REQUIRE(file.has_value());
    // A wrong master key is equivalent to authenticated-object corruption and
    // avoids introducing a test-only write seam into descriptor-safe ObjectFile.
    constexpr std::array<uint8_t, 32> WRONG_KEY{9, 8, 7, 6};
    const auto report = verify_vault(*root, db, WRONG_KEY, ROOT, VerifyDepth::Deep);
    CHECK(report.status == RecoveryStatus::Corrupt);
    REQUIRE(report.findings.size() == 1);
    CHECK(report.findings[0].kind == IntegrityFindingKind::UnauthenticObject);
    CHECK_EQ(report.findings[0].object_id, reference.object_id);
    CHECK_TRUE(root->open_object(reference.object_id).has_value());
}
