#include "test_framework.h"

#include "vault/v3_schema.h"

#ifdef OSV_SQLCIPHER_SPIKE
#include <sqlite3.h>

#include <array>
#include <cstdint>
#include <string>

namespace {

struct MemoryDb {
    sqlite3* value = nullptr;
    MemoryDb() = default;
    MemoryDb(const MemoryDb&) = delete;
    MemoryDb& operator=(const MemoryDb&) = delete;
    MemoryDb(MemoryDb&& other) noexcept : value(other.value) { other.value = nullptr; }
    MemoryDb& operator=(MemoryDb&&) = delete;
    ~MemoryDb() { sqlite3_close(value); }
};

bool exec(sqlite3* db, std::string_view sql)
{
    return sqlite3_exec(db, std::string{sql}.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
}

MemoryDb schema_db()
{
    MemoryDb result;
    if (sqlite3_open(":memory:", &result.value) != SQLITE_OK) return result;
    if (!exec(result.value, vault::v3::SCHEMA_SQL)) {
        sqlite3_close(result.value);
        result.value = nullptr;
    }
    return result;
}

std::string explain_detail(sqlite3* db, const char* sql)
{
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return {};
    std::string result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* text = sqlite3_column_text(stmt, 3);
        if (text != nullptr) result += reinterpret_cast<const char*>(text);
    }
    sqlite3_finalize(stmt);
    return result;
}

} // namespace

TEST(v3_schema_creates_with_foreign_keys_and_expected_version)
{
    auto db = schema_db();
    REQUIRE(db.value != nullptr);
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db.value, "PRAGMA foreign_keys;", -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    CHECK_EQ(sqlite3_column_int(stmt, 0), 1);
    sqlite3_finalize(stmt);
    REQUIRE(sqlite3_prepare_v2(db.value, "PRAGMA user_version;", -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    CHECK_EQ(sqlite3_column_int(stmt, 0), vault::v3::SCHEMA_VERSION);
    sqlite3_finalize(stmt);
}

TEST(v3_schema_rejects_dangling_parent_duplicate_root_and_bad_ids)
{
    auto db = schema_db();
    REQUIRE(db.value != nullptr);
    CHECK_FALSE(exec(db.value, "INSERT INTO nodes(node_id,parent_id,node_type,display_name,sibling_order) VALUES(randomblob(16),randomblob(16),0,'dangling',0);"));
    CHECK_FALSE(exec(db.value, "INSERT INTO nodes(node_id,node_type,display_name,sibling_order) VALUES(zeroblob(16),0,'zero',0);"));
    CHECK(exec(db.value, "INSERT INTO nodes(node_id,node_type,display_name,sibling_order) VALUES(x'01010101010101010101010101010101',0,'root',0);"));
    CHECK_FALSE(exec(db.value, "INSERT INTO nodes(node_id,node_type,display_name,sibling_order) VALUES(x'02020202020202020202020202020202',0,'second root',1);"));
}

TEST(v3_schema_enforces_object_identity_role_and_tag_casefolding)
{
    auto db = schema_db();
    REQUIRE(db.value != nullptr);
    CHECK(exec(db.value, "INSERT INTO nodes(node_id,node_type,display_name,sibling_order) VALUES(x'01010101010101010101010101010101',0,'root',0);"));
    CHECK(exec(db.value, "INSERT INTO nodes(node_id,parent_id,node_type,display_name,sibling_order) VALUES(x'02020202020202020202020202020202',x'01010101010101010101010101010101',1,'photo.jpg',0);"));
    CHECK(exec(db.value, "INSERT INTO objects VALUES(x'03030303030303030303030303030303',x'02020202020202020202020202020202',1,100,60,60,1,0,1);"));
    CHECK_FALSE(exec(db.value, "INSERT INTO objects VALUES(x'04040404040404040404040404040404',x'02020202020202020202020202020202',1,100,60,60,1,0,1);"));
    CHECK(exec(db.value, "INSERT INTO tags(display_name,canonical_name) VALUES('Holiday','holiday');"));
    CHECK_FALSE(exec(db.value, "INSERT INTO tags(display_name,canonical_name) VALUES('HOLIDAY','HOLIDAY');"));
}

TEST(v3_schema_gallery_and_tag_queries_use_frozen_indexes)
{
    auto db = schema_db();
    REQUIRE(db.value != nullptr);
    const auto gallery_plan = explain_detail(db.value,
        "EXPLAIN QUERY PLAN SELECT * FROM nodes WHERE parent_id=? ORDER BY sibling_order;");
    CHECK_TRUE(gallery_plan.find("nodes_by_parent_order") != std::string::npos);
    const auto tag_plan = explain_detail(db.value,
        "EXPLAIN QUERY PLAN SELECT node_id FROM node_tags WHERE tag_id=?;");
    CHECK_TRUE(tag_plan.find("node_tags_by_tag") != std::string::npos);
}

#endif
