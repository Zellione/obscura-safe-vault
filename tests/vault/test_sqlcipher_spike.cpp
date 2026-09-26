#include "test_framework.h"

#ifdef OSV_SQLCIPHER_SPIKE
#include <sqlite3.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

struct DbCloser {
    void operator()(sqlite3* db) const noexcept { sqlite3_close(db); }
};

using DbPtr = std::unique_ptr<sqlite3, DbCloser>;

fs::path temp_dir()
{
    std::array<char, 64> pattern{};
    std::snprintf(pattern.data(), pattern.size(), "/tmp/osv-sqlcipher-XXXXXX");
    char* made = ::mkdtemp(pattern.data());
    return made == nullptr ? fs::path{} : fs::path{made};
}

bool exec(sqlite3* db, const char* sql)
{
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

DbPtr open_keyed(const fs::path& path, std::span<const uint8_t, 32> key)
{
    sqlite3* raw = nullptr;
    if (sqlite3_open_v2(path.c_str(), &raw,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                        nullptr) != SQLITE_OK) {
        sqlite3_close(raw);
        return {};
    }
    DbPtr db{raw};
    if (sqlite3_key(db.get(), key.data(), static_cast<int>(key.size())) != SQLITE_OK)
        return {};
    return db;
}

bool file_contains(const fs::path& path, std::string_view needle)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const std::string bytes{std::istreambuf_iterator<char>{in}, {}};
    return bytes.find(needle) != std::string::npos;
}

} // namespace

TEST(sqlcipher_raw_key_encrypts_database_and_rollback_journal)
{
    const auto dir = temp_dir();
    REQUIRE(!dir.empty());
    const auto db_path = dir / "vault.db";
    constexpr std::array<uint8_t, 32> KEY{
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    };
    constexpr std::string_view CANARY = "phase105-secret-name";
    constexpr std::string_view UPDATED = "phase105-updated-secret";

    auto db = open_keyed(db_path, KEY);
    REQUIRE(db != nullptr);
    CHECK(exec(db.get(), "PRAGMA cipher_memory_security = ON;"));
    CHECK(exec(db.get(), "PRAGMA journal_mode = DELETE;"));
    CHECK(exec(db.get(), "PRAGMA synchronous = FULL;"));
    CHECK(exec(db.get(), "CREATE TABLE metadata(value TEXT NOT NULL);"));
    CHECK(exec(db.get(), "INSERT INTO metadata VALUES('phase105-secret-name');"));
    CHECK_FALSE(file_contains(db_path, CANARY));

    CHECK(exec(db.get(), "BEGIN IMMEDIATE;"));
    CHECK(exec(db.get(), "UPDATE metadata SET value='phase105-updated-secret';"));
    const auto journal_path = fs::path{db_path.string() + "-journal"};
    REQUIRE(fs::exists(journal_path));
    CHECK_FALSE(file_contains(db_path, CANARY));
    CHECK_FALSE(file_contains(db_path, UPDATED));
    CHECK_FALSE(file_contains(journal_path, CANARY));
    CHECK_FALSE(file_contains(journal_path, UPDATED));
    CHECK(exec(db.get(), "ROLLBACK;"));
    db.reset();

    constexpr std::array<uint8_t, 32> WRONG_KEY{0xff};
    auto wrong = open_keyed(db_path, WRONG_KEY);
    REQUIRE(wrong != nullptr);
    CHECK_FALSE(exec(wrong.get(), "SELECT count(*) FROM sqlite_master;"));
    wrong.reset();

    auto reopened = open_keyed(db_path, KEY);
    REQUIRE(reopened != nullptr);
    CHECK(exec(reopened.get(), "SELECT count(*) FROM sqlite_master;"));
    reopened.reset();
    fs::remove_all(dir);
}

#endif
