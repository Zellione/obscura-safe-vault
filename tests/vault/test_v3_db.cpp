#include <sqlite3.h>
#include "test_framework.h"
#include "vault/v3_db.h"
#include "vault/v3_sqlcipher_key.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

fs::path db_temp_dir()
{
    std::array<char, 64> pattern{};
    std::snprintf(pattern.data(), pattern.size(), "/tmp/osv-v3-db-XXXXXX");
    const char* made = ::mkdtemp(pattern.data());
    return made == nullptr ? fs::path{} : fs::path{made};
}

constexpr std::array<uint8_t, 32> DB_KEY{
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};
constexpr vault::v3::Id ROOT_ID{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
constexpr vault::v3::Id CHILD_ID{2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
constexpr vault::v3::Id OBJECT_ID{3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3};

bool contains_text(const fs::path& path, std::string_view needle)
{
    std::ifstream input(path, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>{input}, {}};
    return bytes.find(needle) != std::string::npos;
}

}  // namespace

TEST(v3_db_create_reopen_and_read_root)
{
    const auto dir = db_temp_dir();
    REQUIRE(!dir.empty());
    const auto path = dir / "vault.db";

    auto created = vault::v3::Database::create(path, DB_KEY, ROOT_ID);
    REQUIRE(created.status == vault::v3::DbStatus::Ok);
    REQUIRE(created.database.has_value());
    CHECK_EQ(vault::v3::database_root_node_id(*created.database), ROOT_ID);
    CHECK(vault::v3::database_healthy(*created.database));
    created.database.reset();

    auto reopened = vault::v3::Database::open(path, DB_KEY, false);
    REQUIRE(reopened.status == vault::v3::DbStatus::Ok);
    REQUIRE(reopened.database.has_value());
    CHECK_EQ(vault::v3::database_root_node_id(*reopened.database), ROOT_ID);
    reopened.database.reset();
    fs::remove_all(dir);
}

TEST(v3_db_wrong_key_and_future_version_fail_closed)
{
    const auto dir = db_temp_dir();
    REQUIRE(!dir.empty());
    const auto path = dir / "vault.db";
    auto created = vault::v3::Database::create(path, DB_KEY, ROOT_ID);
    REQUIRE(created.database.has_value());

    constexpr std::array<uint8_t, 32> WRONG_KEY{0xff};
    created.database.reset();
    auto wrong = vault::v3::Database::open(path, WRONG_KEY, false);
    CHECK(wrong.status == vault::v3::DbStatus::WrongKeyOrCorrupt);
    CHECK_FALSE(wrong.database.has_value());

    auto writable = vault::v3::Database::open(path, DB_KEY, true);
    REQUIRE(writable.database.has_value());
    REQUIRE(vault::v3::set_database_user_version_for_test(*writable.database, 999) ==
            vault::v3::DbStatus::Ok);
    writable.database.reset();
    auto future = vault::v3::Database::open(path, DB_KEY, false);
    CHECK(future.status == vault::v3::DbStatus::UnsupportedVersion);
    CHECK_FALSE(future.database.has_value());
    fs::remove_all(dir);
}

TEST(v3_db_constraints_are_typed_and_connection_closes)
{
    const auto dir = db_temp_dir();
    REQUIRE(!dir.empty());
    const auto path = dir / "vault.db";
    auto created = vault::v3::Database::create(path, DB_KEY, ROOT_ID);
    REQUIRE(created.database.has_value());
    vault::v3::NodeRecord invalid;
    invalid.parent_id = ROOT_ID;
    invalid.display_name = "child";
    CHECK(created.database->insert_node(invalid) == vault::v3::DbStatus::InvalidArgument);
    invalid.node_id = ROOT_ID;
    CHECK(created.database->insert_node(invalid) == vault::v3::DbStatus::Constraint);
    created.database.reset();
    CHECK(fs::exists(path));
    fs::remove_all(dir);
}

TEST(v3_db_repository_round_trip_and_encrypted_backup)
{
    const auto dir = db_temp_dir();
    REQUIRE(!dir.empty());
    const auto path = dir / "vault.db";
    const auto backup = dir / "backup.db";
    auto opened = vault::v3::Database::create(path, DB_KEY, ROOT_ID);
    REQUIRE(opened.database.has_value());

    vault::v3::NodeRecord child;
    child.node_id = CHILD_ID;
    child.parent_id = ROOT_ID;
    child.type = vault::v3::NodeType::Image;
    child.display_name = "猫-📷-secret.jpg";
    child.sibling_order = 4;
    child.favorite = true;
    child.created_ts = 1234;
    child.media_format = 0;
    child.width = 1920;
    child.height = 1080;
    child.original_size = 54321;
    child.animated = false;
    CHECK(opened.database->insert_node(child) == vault::v3::DbStatus::Ok);

    vault::v3::ObjectRecord object;
    object.object_id = OBJECT_ID;
    object.node_id = CHILD_ID;
    object.encrypted_length = 60000;
    object.plaintext_length = 54321;
    object.frame_plain_limit = 1048576;
    object.frame_count = 1;
    object.creation_generation = 2;
    CHECK(opened.database->insert_object(object) == vault::v3::DbStatus::Ok);
    CHECK(opened.database->add_tag(1, "place:東京", "place:東京") == vault::v3::DbStatus::Ok);
    CHECK(opened.database->assign_tag(CHILD_ID, 1) == vault::v3::DbStatus::Ok);
    vault::v3::TagCategoryRecord category;
    category.category_id = 1;
    category.display_name = "place";
    category.swatch = 5;
    category.fields.emplace_back("country");
    category.fields.emplace_back("city");
    CHECK(opened.database->add_tag_category(category) == vault::v3::DbStatus::Ok);
    vault::v3::TagFieldValueRecord field_value;
    field_value.tag_id = 1;
    field_value.category_id = 1;
    field_value.field_order = 1;
    field_value.value = "東京";
    CHECK(opened.database->set_tag_field_value(field_value) == vault::v3::DbStatus::Ok);
    CHECK(opened.database->set_tag_description(1, "secret-description") == vault::v3::DbStatus::Ok);

    vault::v3::SavedSearchRecord search;
    search.search_id = 1;
    search.display_name = "secret-search";
    REQUIRE(search.query.assign({reinterpret_cast<const uint8_t*>("secret-query"), 12}));
    CHECK(opened.database->add_saved_search(search) == vault::v3::DbStatus::Ok);
    CHECK(opened.database->set_settings({7, 2, false, 13, 17, 512}) == vault::v3::DbStatus::Ok);

    const auto children = opened.database->list_children(ROOT_ID);
    REQUIRE(children.status == vault::v3::DbStatus::Ok);
    REQUIRE(children.value.size() == 1);
    CHECK(children.value[0].display_name.view() == "猫-📷-secret.jpg");
    CHECK(children.value[0].favorite);
    const auto objects = opened.database->object_references();
    REQUIRE(objects.status == vault::v3::DbStatus::Ok);
    REQUIRE(objects.value.size() == 1);
    CHECK_EQ(objects.value[0].object_id, OBJECT_ID);
    const auto tags = opened.database->node_tags(CHILD_ID);
    REQUIRE(tags.value.size() == 1);
    CHECK(tags.value[0].view() == "place:東京");
    const auto search_by_name = opened.database->search_nodes("📷");
    REQUIRE(search_by_name.value.size() == 1);
    CHECK_EQ(search_by_name.value[0].node_id, CHILD_ID);
    const auto search_by_tag = opened.database->search_nodes("東京");
    REQUIRE(search_by_tag.value.size() == 1);
    const auto categories = opened.database->tag_categories();
    REQUIRE(categories.value.size() == 1);
    REQUIRE(categories.value[0].fields.size() == 2);
    CHECK(categories.value[0].fields[1].view() == "city");
    const auto values = opened.database->tag_field_values(1);
    REQUIRE(values.value.size() == 1);
    CHECK(values.value[0].value.view() == "東京");
    const auto description = opened.database->tag_description(1);
    REQUIRE(description.value.has_value());
    CHECK(description.value->view() == "secret-description");
    const auto searches = opened.database->saved_searches();
    REQUIRE(searches.value.size() == 1);
    CHECK(searches.value[0].display_name.view() == "secret-search");
    const auto settings = opened.database->settings();
    CHECK_EQ(settings.value.migrated_thumb_side, 512);

    REQUIRE(opened.database->backup_to(backup, DB_KEY) == vault::v3::DbStatus::Ok);
    CHECK_FALSE(contains_text(path, "secret"));
    CHECK_FALSE(contains_text(backup, "secret"));
    opened.database.reset();
    auto backup_open = vault::v3::Database::open(backup, DB_KEY, false);
    REQUIRE(backup_open.database.has_value());
    CHECK(backup_open.database->find_node(CHILD_ID).value.has_value());
    backup_open.database.reset();
    fs::remove_all(dir);
}

TEST(v3_db_deep_integrity_and_lossless_maintenance_preserve_rows)
{
    const auto dir = db_temp_dir();
    REQUIRE(!dir.empty());
    const auto path = dir / "vault.db";
    auto opened = vault::v3::Database::create(path, DB_KEY, ROOT_ID);
    REQUIRE(opened.database.has_value());
    auto& db = *opened.database;
    REQUIRE(vault::v3::database_deep_healthy(db));

    vault::v3::NodeRecord child;
    child.node_id = CHILD_ID;
    child.parent_id = ROOT_ID;
    child.type = vault::v3::NodeType::Image;
    child.display_name = "preserved.jpg";
    child.media_format = 1;
    REQUIRE(db.insert_node(child) == vault::v3::DbStatus::Ok);
    REQUIRE(vault::v3::maintain_database(db) == vault::v3::DbStatus::Ok);
    REQUIRE(vault::v3::database_deep_healthy(db));
    const auto found = db.find_node(CHILD_ID);
    REQUIRE(found.status == vault::v3::DbStatus::Ok);
    REQUIRE(found.value.has_value());
    CHECK(found.value->display_name == "preserved.jpg");
    opened.database.reset();
    fs::remove_all(dir);
}

TEST(v3_db_migrates_v1_transactionally_and_preserves_encrypted_content)
{
    const auto dir = db_temp_dir();
    const auto path = dir / "vault.db";
    std::ifstream schema_file(std::filesystem::path(OSV_VAULT_FIXTURE_DIR) / "v3_schema_v1.sql");
    const std::string schema{std::istreambuf_iterator<char>{schema_file}, {}};
    REQUIRE(!schema.empty());
    sqlite3* raw = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &raw) == SQLITE_OK);
    auto key = vault::v3::sqlcipher_raw_keyspec(DB_KEY);
    REQUIRE(sqlite3_key(raw, key.data(), static_cast<int>(key.size())) == SQLITE_OK);
    REQUIRE(sqlite3_exec(raw, schema.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_exec(raw,
                         "INSERT INTO vault_meta(singleton,schema_version,root_node_id) "
                         "VALUES(1,1,X'01000000000000000000000000000001');"
                         "INSERT INTO nodes(node_id,node_type,display_name,sibling_order) "
                         "VALUES(X'01000000000000000000000000000001',0,'root',0);"
                         "INSERT INTO "
                         "nodes(node_id,parent_id,node_type,display_name,sibling_order,duration_ms,"
                         "original_size,media_format) "
                         "VALUES(X'02000000000000000000000000000002',X'"
                         "01000000000000000000000000000001',2,'old-video',0,1234,4,0);"
                         "INSERT INTO tags VALUES(1,'Blue','blue');"
                         "INSERT INTO node_tags VALUES(X'02000000000000000000000000000002',1);",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_close(raw) == SQLITE_OK);
    const auto encrypted_before = [&] {
        std::ifstream input(path, std::ios::binary);
        return std::string{std::istreambuf_iterator<char>{input}, {}};
    }();
    auto snapshot = vault::v3::Database::open(path, DB_KEY, false);
    REQUIRE(snapshot.database);
    CHECK(snapshot.database->find_node(CHILD_ID).value->duration_us == 1'234'000);
    snapshot.database.reset();
    std::ifstream unchanged(path, std::ios::binary);
    const std::string encrypted_after{std::istreambuf_iterator<char>{unchanged}, {}};
    CHECK(encrypted_before == encrypted_after);
    auto migrated = vault::v3::Database::open(path, DB_KEY, true);
    REQUIRE(migrated.database);
    CHECK(vault::v3::database_deep_healthy(*migrated.database));
    auto video = migrated.database->find_node(CHILD_ID);
    REQUIRE(video.value);
    CHECK(video.value->duration_us == 1'234'000);
    auto tags = migrated.database->node_tags(CHILD_ID);
    REQUIRE(tags.value.size() == 1);
    CHECK(tags.value[0] == "Blue");
    vault::v3::NodeRecord sibling = *video.value;
    sibling.node_id[0] = 9;
    sibling.display_name = "OLD-VIDEO";
    sibling.sibling_order = 1;
    REQUIRE(migrated.database->insert_node(sibling) == vault::v3::DbStatus::Ok);
    REQUIRE(migrated.database->add_tag(2, std::string(1024, 'x'), std::string(1024, 'x')) ==
            vault::v3::DbStatus::Ok);
    migrated.database.reset();
    auto reopened = vault::v3::Database::open(path, DB_KEY, false);
    REQUIRE(reopened.database);
    CHECK(vault::v3::database_deep_healthy(*reopened.database));
    CHECK_FALSE(contains_text(path, "old-video"));
    reopened.database.reset();
    fs::remove_all(dir);
}

TEST(v3_db_tag_limit_is_per_node_and_failed_sync_rolls_back)
{
    const auto dir = db_temp_dir();
    auto opened = vault::v3::Database::create(dir / "vault.db", DB_KEY, ROOT_ID);
    REQUIRE(opened.database);
    auto root = vault::IndexNode::gallery("");
    root.node_id = ROOT_ID;
    for (size_t i = 0; i <= vault::INDEX_MAX_TAGS; ++i)
        root.tags.emplace_back("tag-" + std::to_string(i));
    CHECK(opened.database->sync_metadata(root, {}, {}) == vault::v3::DbStatus::Constraint);
    CHECK(opened.database->node_tags(ROOT_ID).value.empty());
    CHECK(vault::v3::database_deep_healthy(*opened.database));
    opened.database.reset();
    fs::remove_all(dir);
}
