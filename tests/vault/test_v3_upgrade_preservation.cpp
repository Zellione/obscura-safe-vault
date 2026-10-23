#include "test_framework.h"
#include "vault/v3_backup.h"
#include "vault/v3_object_store.h"
#include "vault/v3_schema.h"
#include "vault/v3_sqlcipher_key.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <sqlite3.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
namespace fs = std::filesystem;
using namespace vault::v3;
constexpr Id ROOT{1}, OUTER{2}, INNER{3}, FIRST{4}, SECOND{5};
constexpr std::array<uint8_t, 2> PASSWORD{'p', 'w'};
constexpr std::array<std::array<uint8_t, 4>, 3> CONTENT{{{1, 3, 5, 7}, {2, 4, 6, 8}, {9, 8, 7, 6}}};

std::string read_ciphertext(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{file}, {}};
}

using Inventory = std::map<fs::path, std::string>;
Inventory object_inventory(const fs::path& path)
{
    Inventory result;
    for (const auto& entry : fs::recursive_directory_iterator(path / "objects"))
        if (entry.is_regular_file())
            result.emplace(entry.path().lexically_relative(path), read_ciphertext(entry.path()));
    return result;
}

std::string sql_id(const Id& id)
{
    std::string result = "X'";
    for (const auto byte : id)
        result += std::format("{:02x}", byte);
    return result + "'";
}

struct RawDb {
    sqlite3* db = nullptr;
    RawDb() = default;
    RawDb(const RawDb&) = delete;
    RawDb& operator=(const RawDb&) = delete;
    ~RawDb()
    {
        sqlite3_close(db);
    }
    bool open(const fs::path& path, std::span<const uint8_t, crypto::KEY_SIZE> key)
    {
        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) return false;
        const auto spec = sqlcipher_raw_keyspec(key);
        return sqlite3_key(db, spec.data(), static_cast<int>(spec.size())) == SQLITE_OK;
    }
    bool exec(const std::string& sql)
    {
        return sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
    }
    int scalar(const char* sql)
    {
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) return -1;
        const int value =
            sqlite3_step(statement) == SQLITE_ROW ? sqlite3_column_int(statement, 0) : -1;
        sqlite3_finalize(statement);
        return value;
    }
};

struct OldVault {
    fs::path temp;
    fs::path path;
    std::optional<VaultRoot> root;
    V3Header header;
    crypto::SecureBuffer<crypto::KEY_SIZE> master;
    std::array<ObjectInfo, 3> objects{};
    ~OldVault()
    {
        root.reset();
        std::error_code ignored;
        if (!temp.empty()) fs::remove_all(temp, ignored);
    }
    bool create()
    {
        std::array<char, 40> pattern{};
        std::snprintf(pattern.data(), pattern.size(), "/tmp/osv-upgrade-XXXXXX");
        const char* made = ::mkdtemp(pattern.data());
        if (!made) return false;
        temp = made;
        path = temp / "source.osv";
        root = VaultRoot::create(path);
        if (!root ||
            create_v3_header(PASSWORD, {}, {1, 8, 1}, header, master) != HeaderStatus::Ok ||
            !root->write_header(serialize_v3_header(header)))
            return false;
        const auto key = derive_database_key(master.as_span(), header.vault_id);
        RawDb raw;
        if (!raw.open(path / "vault.db", key.as_span()) ||
            ::chmod((path / "vault.db").c_str(), 0600) != 0)
            return false;
        const auto schema = read_ciphertext(fs::path(OSV_VAULT_FIXTURE_DIR) / "v3_schema_v1.sql");
        if (schema.empty() || !raw.exec(schema)) return false;
        if (!raw.exec(std::format(
                "INSERT INTO vault_meta(singleton,schema_version,root_node_id,app_generation) "
                "VALUES(1,1,{},7);"
                "INSERT INTO nodes(node_id,node_type,display_name,sibling_order) "
                "VALUES({},0,'root',0);",
                sql_id(ROOT), sql_id(ROOT))))
            return false;
        if (!insert_node(raw, OUTER, ROOT, "private outer", 0) ||
            !insert_node(raw, INNER, OUTER, "private inner", 0) ||
            !insert_node(raw, FIRST, OUTER, "portrait.jpg", 1) ||
            !insert_node(raw, SECOND, INNER, "portrait.jpg", 1))
            return false;
        if (!raw.exec(std::format(
                "INSERT INTO tags VALUES(1,'Color:Red','color:red'),(2,'Color:Blue','color:blue');"
                "INSERT INTO node_tags VALUES({},1),({},2);"
                "INSERT INTO tag_descriptions VALUES(1,'private description');",
                sql_id(FIRST), sql_id(SECOND))))
            return false;
        for (size_t i = 0; i < objects.size(); ++i) {
            const auto role = i == 2 ? ObjectRole::Thumbnail : ObjectRole::OriginalImage;
            const auto node = i == 1 ? SECOND : FIRST;
            const auto result =
                write_object(*root, master.as_span(), {header.vault_id, node, role, 1}, CONTENT[i]);
            if (result.status != ObjectStatus::Ok) return false;
            objects[i] = result.info;
            if (!raw.exec(std::format("INSERT INTO objects VALUES({},{},{},{},{},{},{},7,1)",
                                      sql_id(result.info.object_id), sql_id(node),
                                      static_cast<int>(role), result.info.encrypted_length,
                                      result.info.plaintext_length, result.info.frame_plain_limit,
                                      result.info.frame_count)))
                return false;
        }
        return true;
    }
    static bool insert_node(RawDb& raw, const Id& id, const Id& parent, const char* name, int type)
    {
        return raw.exec(std::format("INSERT INTO "
                                    "nodes(node_id,parent_id,node_type,display_name,sibling_order,"
                                    "media_format,original_size) "
                                    "VALUES({},{},{},'{}',{},1,4)",
                                    sql_id(id), sql_id(parent), type, name, type));
    }
};

void check_mappings(const OldVault& fixture, const VaultRoot& root, const Database& db,
                    int& _failures)
{
    REQUIRE(database_deep_healthy(db));
    CHECK(database_root_node_id(db) == ROOT);
    CHECK(db.settings().value.app_generation == 7);
    const auto outer = db.find_node(OUTER);
    const auto inner = db.find_node(INNER);
    REQUIRE(outer.value && inner.value);
    CHECK(outer.value->parent_id == ROOT);
    CHECK(outer.value->display_name == "private outer");
    CHECK(inner.value->parent_id == OUTER);
    CHECK(inner.value->display_name == "private inner");
    const auto first = db.find_node(FIRST);
    const auto second = db.find_node(SECOND);
    REQUIRE(first.value && second.value);
    CHECK(first.value->parent_id == OUTER);
    CHECK(second.value->parent_id == INNER);
    CHECK(first.value->display_name == "portrait.jpg");
    CHECK(second.value->display_name == "portrait.jpg");
    const auto red = db.node_tags(FIRST);
    const auto blue = db.node_tags(SECOND);
    REQUIRE(red.value.size() == 1 && blue.value.size() == 1);
    CHECK(red.value[0] == "Color:Red");
    CHECK(blue.value[0] == "Color:Blue");
    const auto description = db.tag_description(1);
    REQUIRE(description.value);
    CHECK(*description.value == "private description");
    const auto refs = db.object_references();
    REQUIRE(refs.status == DbStatus::Ok && refs.value.size() == fixture.objects.size());
    for (size_t i = 0; i < fixture.objects.size(); ++i) {
        const auto& expected = fixture.objects[i];
        const auto found =
            std::ranges::find(refs.value, expected.object_id, &ObjectRecord::object_id);
        REQUIRE(found != refs.value.end());
        CHECK(found->node_id == expected.owner_node_id);
        CHECK(found->role == expected.role);
        auto reader = ObjectReader::open(root, fixture.master.as_span(), expected);
        REQUIRE(reader.reader);
        crypto::SecureBytes plaintext;
        REQUIRE(reader.reader->read_all(plaintext) == ObjectStatus::Ok);
        CHECK_BYTES_EQ(plaintext.as_span(), std::span(CONTENT[i]));
    }
    CHECK(
        verify_vault(root, db, fixture.master.as_span(), fixture.header.vault_id, VerifyDepth::Deep)
            .status == RecoveryStatus::Ok);
}
}  // namespace

TEST(v3_schema_upgrade_failure_retry_and_reopen_preserve_original_ciphertext)
{
    OldVault fixture;
    REQUIRE(fixture.create());
    auto writer = fixture.root->try_writer_lock();
    REQUIRE(writer);
    const auto key = derive_database_key(fixture.master.as_span(), fixture.header.vault_id);
    const auto before = object_inventory(fixture.path);
    REQUIRE(before.size() == 3);
    const auto header = read_ciphertext(fixture.path / "vault.header");
    {
        RawDb raw;
        REQUIRE(raw.open(fixture.path / "vault.db", key.as_span()));
        // The second table rebuild fails AFTER vault_meta was rebuilt in the
        // transaction. No production seam: a real SQL DDL error forces rollback.
        REQUIRE(raw.exec("CREATE TABLE nodes_new(block_migration INTEGER)"));
    }
    const auto failed = Database::open_writable(*fixture.root, key.as_span());
    REQUIRE(failed.status != DbStatus::Ok && !failed.database);
    CHECK(object_inventory(fixture.path) == before);
    CHECK(read_ciphertext(fixture.path / "vault.header") == header);
    {
        RawDb cold;
        REQUIRE(cold.open(fixture.path / "vault.db", key.as_span()));
        CHECK(cold.scalar("PRAGMA user_version") == 1);  // frozen v1 input, not current schema
        CHECK(cold.scalar("SELECT schema_version FROM vault_meta") == 1);
        CHECK(cold.scalar("SELECT count(*) FROM nodes") == 5);
        CHECK(cold.scalar("SELECT count(*) FROM objects") == 3);
        CHECK(cold.scalar("SELECT count(*) FROM node_tags") == 2);
        REQUIRE(cold.exec("DROP TABLE nodes_new"));
    }
    {
        auto migrated = Database::open_writable(*fixture.root, key.as_span());
        REQUIRE(migrated.database);
        check_mappings(fixture, *fixture.root, *migrated.database, _failures);
    }
    writer.reset();
    fixture.root.reset();
    fixture.root = VaultRoot::open(fixture.path);
    REQUIRE(fixture.root);
    auto cold = Database::open_read_only(*fixture.root, key.as_span());
    REQUIRE(cold.database);
    check_mappings(fixture, *fixture.root, *cold.database, _failures);
    RawDb raw;
    REQUIRE(raw.open(fixture.path / "vault.db", key.as_span()));
    CHECK(raw.scalar("PRAGMA user_version") == SCHEMA_VERSION);
    CHECK(raw.scalar("SELECT schema_version FROM vault_meta") == SCHEMA_VERSION);
    CHECK(object_inventory(fixture.path) == before);
    CHECK(read_ciphertext(fixture.path / "vault.header") == header);
}

TEST(v3_encrypted_backup_restore_preserves_nested_mapping_and_immutable_v1_source)
{
    OldVault fixture;
    REQUIRE(fixture.create());
    auto writer = fixture.root->try_writer_lock();
    REQUIRE(writer);
    const auto key = derive_database_key(fixture.master.as_span(), fixture.header.vault_id);
    const auto before = object_inventory(fixture.path);
    REQUIRE(before.size() == 3);
    const auto source_db = read_ciphertext(fixture.path / "vault.db");
    const auto header = read_ciphertext(fixture.path / "vault.header");
    // Read-only old-schema inspection migrates a private in-memory copy. Backup
    // must never rewrite the older source, including its encrypted database.
    auto old = Database::open_read_only(*fixture.root, key.as_span());
    REQUIRE(old.database);
    const auto backup_path = fixture.temp / "backup.osv";
    const auto backup = backup_vault(*fixture.root, *writer, *old.database, key.as_span(),
                                     fixture.master.as_span(), fixture.header, backup_path);
    REQUIRE(backup.status == BackupStatus::Ok);
    CHECK(backup.copied_objects == 3);
    old.database.reset();
    CHECK(read_ciphertext(fixture.path / "vault.db") == source_db);
    CHECK(object_inventory(fixture.path) == before);
    CHECK(object_inventory(backup_path) == before);
    const auto backup_db = read_ciphertext(backup_path / "vault.db");
    REQUIRE(!backup_db.empty());
    CHECK(!backup_db.starts_with("SQLite format 3"));
    CHECK(backup_db.find("private outer") == std::string::npos);
    CHECK(backup_db.find("portrait.jpg") == std::string::npos);
    const auto restored_path = fixture.temp / "restored.osv";
    REQUIRE(restore_vault(backup_path, PASSWORD, {}, restored_path).status == BackupStatus::Ok);
    CHECK(read_ciphertext(backup_path / "vault.db") == backup_db);
    CHECK(object_inventory(backup_path) == before);
    CHECK(object_inventory(restored_path) == before);
    CHECK(read_ciphertext(restored_path / "vault.header") == header);
    auto restored = VaultRoot::open(restored_path);
    REQUIRE(restored);
    auto database = Database::open_read_only(*restored, key.as_span());
    REQUIRE(database.database);
    check_mappings(fixture, *restored, *database.database, _failures);
    crypto::SecureBuffer<crypto::KEY_SIZE> wrong_key;
    CHECK(!Database::open_read_only(*restored, wrong_key.as_span()).database);
}
