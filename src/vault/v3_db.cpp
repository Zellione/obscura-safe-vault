#include "vault/v3_db.h"

#include "crypto/random.h"
#include "vault/v3_schema.h"
#include "vault/v3_sqlcipher_key.h"
#include "vault/v3_fs.h"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <fcntl.h>
#include <format>
#include <limits>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <utility>

namespace vault::v3 {
using enum DbStatus;
namespace {

class Statement {
public:
    Statement(sqlite3* db, const char* sql) noexcept
    {
        if (sqlite3_prepare_v2(db, sql, -1, &value_, nullptr) != SQLITE_OK) value_ = nullptr;
    }
    ~Statement()
    {
        sqlite3_finalize(value_);
    }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    [[nodiscard]] sqlite3_stmt* get() const noexcept
    {
        return value_;
    }

private:
    sqlite3_stmt* value_ = nullptr;
};

DbStatus map_status(int code) noexcept
{
    switch (code & 0xff) {
    case SQLITE_OK:
    case SQLITE_DONE:
        return Ok;
    case SQLITE_BUSY:
    case SQLITE_LOCKED:
        return Busy;
    case SQLITE_FULL:
        return DiskFull;
    case SQLITE_CONSTRAINT:
        return Constraint;
    case SQLITE_NOTADB:
    case SQLITE_CORRUPT:
        return WrongKeyOrCorrupt;
    default:
        return IoError;
    }
}

DbStatus exec(sqlite3* db, std::string_view sql) noexcept
{
    return map_status(sqlite3_exec(db, std::string{sql}.c_str(), nullptr, nullptr, nullptr));
}

bool id_valid(const Id& id) noexcept
{
    return std::ranges::any_of(id, [](uint8_t byte) { return byte != 0; });
}

void bind_id(sqlite3_stmt* statement, int index, const Id& id) noexcept
{
    sqlite3_bind_blob(statement, index, id.data(), static_cast<int>(id.size()), SQLITE_TRANSIENT);
}

std::optional<Id> column_id(sqlite3_stmt* statement, int column) noexcept
{
    if (sqlite3_column_type(statement, column) == SQLITE_NULL) return std::nullopt;
    if (sqlite3_column_bytes(statement, column) != static_cast<int>(Id{}.size()))
        return std::nullopt;
    Id id{};
    const auto* bytes = static_cast<const uint8_t*>(sqlite3_column_blob(statement, column));
    if (bytes == nullptr) return std::nullopt;
    std::copy_n(bytes, id.size(), id.begin());
    return id;
}

template <typename T>
std::optional<T> optional_integer(sqlite3_stmt* statement, int column) noexcept
{
    if (sqlite3_column_type(statement, column) == SQLITE_NULL) return std::nullopt;
    const auto value = sqlite3_column_int64(statement, column);
    if (value < 0 || static_cast<uint64_t>(value) > std::numeric_limits<T>::max())
        return std::nullopt;
    return static_cast<T>(value);
}

std::optional<NodeRecord> read_node(sqlite3_stmt* statement)
{
    const auto id = column_id(statement, 0);
    if (!id || !id_valid(*id)) return std::nullopt;
    const int raw_type = sqlite3_column_int(statement, 2);
    const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3));
    const int name_size = sqlite3_column_bytes(statement, 3);
    const auto order = sqlite3_column_int64(statement, 4);
    const auto created = sqlite3_column_int64(statement, 6);
    if (raw_type < 0 || raw_type > 2 || name == nullptr || name_size < 1 || order < 0 ||
        created < 0)
        return std::nullopt;
    NodeRecord node;
    node.node_id = *id;
    if (sqlite3_column_type(statement, 1) != SQLITE_NULL) {
        node.parent_id = column_id(statement, 1);
        if (!node.parent_id) return std::nullopt;
    }
    node.type = static_cast<NodeType>(raw_type);
    node.display_name = std::string_view{name, static_cast<size_t>(name_size)};
    node.sibling_order = static_cast<uint64_t>(order);
    node.favorite = sqlite3_column_int(statement, 5) != 0;
    node.created_ts = static_cast<uint64_t>(created);
    node.media_format = optional_integer<int>(statement, 7);
    node.width = optional_integer<uint32_t>(statement, 8);
    node.height = optional_integer<uint32_t>(statement, 9);
    node.duration_ms = optional_integer<uint64_t>(statement, 10);
    node.codec = optional_integer<int>(statement, 11);
    node.original_size = optional_integer<uint64_t>(statement, 12);
    node.sort_key = optional_integer<uint8_t>(statement, 13);
    if (sqlite3_column_type(statement, 14) != SQLITE_NULL)
        node.animated = sqlite3_column_int(statement, 14) != 0;
    return node;
}

constexpr const char* NODE_COLUMNS =
    "node_id,parent_id,node_type,display_name,sibling_order,favorite,created_ts,"
    "media_format,width,height,duration_ms,codec,original_size,sort_key,animated";
constexpr const char* NODE_COLUMNS_N =
    "n.node_id,n.parent_id,n.node_type,n.display_name,n.sibling_order,n.favorite,n.created_ts,"
    "n.media_format,n.width,n.height,n.duration_ms,n.codec,n.original_size,n.sort_key,n.animated";

bool count_below(sqlite3* db, const char* sql, int64_t limit) noexcept
{
    Statement statement{db, sql};
    return statement.get() && sqlite3_step(statement.get()) == SQLITE_ROW &&
           sqlite3_column_int64(statement.get(), 0) < limit;
}

bool parent_depth_allowed(sqlite3* db, const Id& parent_id) noexcept
{
    Statement statement{db, "WITH RECURSIVE ancestors(id,parent_id,depth) AS ("
                            "SELECT node_id,parent_id,0 FROM nodes WHERE node_id=? UNION ALL "
                            "SELECT n.node_id,n.parent_id,a.depth+1 FROM nodes n JOIN ancestors a "
                            "ON n.node_id=a.parent_id WHERE a.depth<128) "
                            "SELECT max(depth) FROM ancestors"};
    if (!statement.get()) return false;
    bind_id(statement.get(), 1, parent_id);
    return sqlite3_step(statement.get()) == SQLITE_ROW &&
           sqlite3_column_type(statement.get(), 0) != SQLITE_NULL &&
           sqlite3_column_int(statement.get(), 0) < 128;
}

DbStatus configure(sqlite3* db, std::span<const uint8_t, crypto::KEY_SIZE> database_key) noexcept
{
    auto keyspec = sqlcipher_raw_keyspec(database_key);
    auto status =
        map_status(sqlite3_key(db, keyspec.data(), static_cast<int>(decltype(keyspec)::size())));
    if (status != Ok) return status;

    sqlite3_extended_result_codes(db, 1);
    sqlite3_busy_timeout(db, 5000);
    // Loadable-extension support is omitted from the pinned SQLCipher build,
    // so the enabling API is intentionally not linked into the application.
    if (int ignored = 0;
        sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 1, &ignored) != SQLITE_OK ||
        sqlite3_db_config(db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, &ignored) != SQLITE_OK)
        return IoError;

    sqlite3_limit(db, SQLITE_LIMIT_LENGTH, 2 * 1024 * 1024);
    sqlite3_limit(db, SQLITE_LIMIT_SQL_LENGTH, 128 * 1024);
    sqlite3_limit(db, SQLITE_LIMIT_COLUMN, 128);
    sqlite3_limit(db, SQLITE_LIMIT_EXPR_DEPTH, 64);
    sqlite3_limit(db, SQLITE_LIMIT_ATTACHED, 0);

    if (constexpr std::string_view pragmas = "PRAGMA cipher_memory_security=ON;"
                                             "PRAGMA foreign_keys=ON;"
                                             "PRAGMA trusted_schema=OFF;"
                                             "PRAGMA temp_store=MEMORY;"
                                             "PRAGMA journal_mode=DELETE;"
                                             "PRAGMA synchronous=FULL;";
        (status = exec(db, pragmas)) != Ok)
        return status;

    Statement verify{db, "SELECT count(*) FROM sqlite_schema"};
    if (verify.get() == nullptr) return map_status(sqlite3_extended_errcode(db));
    if (sqlite3_step(verify.get()) != SQLITE_ROW) return map_status(sqlite3_extended_errcode(db));
    return Ok;
}

int user_version(sqlite3* db) noexcept
{
    Statement statement{db, "PRAGMA user_version"};
    if (statement.get() == nullptr || sqlite3_step(statement.get()) != SQLITE_ROW) return -1;
    return sqlite3_column_int(statement.get(), 0);
}

void remove_file(const std::filesystem::path& path) noexcept
{
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

DbStatus copy_database(sqlite3* source, sqlite3* target) noexcept
{
    sqlite3_backup* backup = sqlite3_backup_init(target, "main", source, "main");
    if (!backup) return map_status(sqlite3_extended_errcode(target));
    int step = SQLITE_OK;
    do {
        step = sqlite3_backup_step(backup, 128);
        if (step == SQLITE_BUSY || step == SQLITE_LOCKED) sqlite3_sleep(10);
    } while (step == SQLITE_OK || step == SQLITE_BUSY || step == SQLITE_LOCKED);
    const int finished = sqlite3_backup_finish(backup);
    return map_status(step == SQLITE_DONE ? finished : step);
}

bool sync_file(const std::filesystem::path& path) noexcept
{
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    const bool synced = ::fsync(fd) == 0;
    ::close(fd);
    return synced;
}

bool publish_backup(const std::filesystem::path& temporary,
                    const std::filesystem::path& destination,
                    const std::filesystem::path& parent) noexcept
{
    if (!sync_file(temporary) || ::link(temporary.c_str(), destination.c_str()) != 0) return false;
    const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) {
        remove_file(destination);
        return false;
    }
    const bool published = ::fsync(directory) == 0;
    if (published) ::unlink(temporary.c_str());
    const bool cleaned = published && ::fsync(directory) == 0;
    ::close(directory);
    if (!cleaned) remove_file(destination);
    return cleaned;
}

}  // namespace

Database::~Database()
{
    if (handle_ != nullptr) sqlite3_close(handle_);
}

Database::Database(Database&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

Database& Database::operator=(Database&& other) noexcept
{
    if (this != &other) {
        if (handle_ != nullptr) sqlite3_close(handle_);
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

Database::OpenResult Database::open_raw(const std::filesystem::path& path,
                                        std::span<const uint8_t, crypto::KEY_SIZE> key,
                                        int flags) noexcept
{
    sqlite3* raw = nullptr;
    if (const int opened =
            sqlite3_open_v2(path.c_str(), &raw, flags | SQLITE_OPEN_FULLMUTEX, nullptr);
        opened != SQLITE_OK) {
        const auto status = map_status(opened);
        sqlite3_close(raw);
        return {status, std::nullopt};
    }
    Database db{raw};
    if (const auto status = configure(raw, key); status != Ok) return {status, std::nullopt};
    return {Ok, std::move(db)};
}

Database::OpenResult Database::create(const std::filesystem::path& path,
                                      std::span<const uint8_t, crypto::KEY_SIZE> database_key,
                                      const Id& root_node_id) noexcept
{
    if (!id_valid(root_node_id)) return {InvalidArgument, std::nullopt};
    const int claimed = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (claimed < 0) return {IoError, std::nullopt};
    ::close(claimed);
    auto result = open_raw(path, database_key, SQLITE_OPEN_READWRITE);
    if (!result.database) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        return result;
    }
    auto* db = result.database->handle_;
    if (exec(db, "BEGIN IMMEDIATE") != Ok || exec(db, SCHEMA_SQL) != Ok) {
        exec(db, "ROLLBACK");
        const auto status = map_status(sqlite3_extended_errcode(db));
        result.database.reset();
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        return {status, std::nullopt};
    }
    Statement meta{db,
                   "INSERT INTO vault_meta(singleton,schema_version,root_node_id) VALUES(1,1,?)"};
    if (Statement root{
            db,
            "INSERT INTO nodes(node_id,parent_id,node_type,display_name,sibling_order,sort_key) "
            "VALUES(?,NULL,0,'/',0,7)"};
        !meta.get() || !root.get() ||
        sqlite3_bind_blob(meta.get(), 1, root_node_id.data(), static_cast<int>(root_node_id.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_blob(root.get(), 1, root_node_id.data(), static_cast<int>(root_node_id.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(meta.get()) != SQLITE_DONE || sqlite3_step(root.get()) != SQLITE_DONE ||
        exec(db, "COMMIT") != Ok) {
        const auto status = map_status(sqlite3_extended_errcode(db));
        exec(db, "ROLLBACK");
        result.database.reset();
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        return {status, std::nullopt};
    }
    return result;
}

Database::OpenResult Database::open(const std::filesystem::path& path,
                                    std::span<const uint8_t, crypto::KEY_SIZE> database_key,
                                    bool writable) noexcept
{
    auto result =
        open_raw(path, database_key, writable ? SQLITE_OPEN_READWRITE : SQLITE_OPEN_READONLY);
    if (!result.database) return result;
    const int version = user_version(result.database->handle_);
    if (version < 0) return {WrongKeyOrCorrupt, std::nullopt};
    if (version > SCHEMA_VERSION) return {UnsupportedVersion, std::nullopt};
    if (version < SCHEMA_VERSION) return {UnsupportedVersion, std::nullopt};
    if (!database_healthy(*result.database)) return {WrongKeyOrCorrupt, std::nullopt};
    return result;
}

Database::OpenResult Database::open_read_only(
    const VaultRoot& root,
    std::span<const uint8_t, crypto::KEY_SIZE> database_key) noexcept
{
    const int fd = ::openat(root.native_handle(), "vault.db",
                            O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (struct stat st{}; fd < 0 || ::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
                          st.st_nlink != 1 || (st.st_mode & 0777) != 0600 ||
                          st.st_uid != ::geteuid()) {
        if (fd >= 0) ::close(fd);
        return {IoError, std::nullopt};
    }
    const std::filesystem::path pinned =
        std::filesystem::path{"/proc/self/fd"} / std::to_string(fd);
    auto result = open(pinned, database_key, false);
    ::close(fd);
    return result;
}

bool database_healthy(const Database& database) noexcept
{
    if (!database.handle_) return false;
    Statement statement{database.handle_, "PRAGMA quick_check"};
    if (!statement.get() || sqlite3_step(statement.get()) != SQLITE_ROW) return false;
    const auto* text = sqlite3_column_text(statement.get(), 0);
    return text != nullptr && std::string_view{reinterpret_cast<const char*>(text)} == "ok";
}

Id database_root_node_id(const Database& database) noexcept
{
    Id result{};
    Statement statement{database.handle_, "SELECT root_node_id FROM vault_meta WHERE singleton=1"};
    if (!statement.get() || sqlite3_step(statement.get()) != SQLITE_ROW ||
        sqlite3_column_bytes(statement.get(), 0) != static_cast<int>(result.size()))
        return {};
    if (const auto* bytes = static_cast<const uint8_t*>(sqlite3_column_blob(statement.get(), 0));
        bytes)
        std::copy_n(bytes, result.size(), result.begin());
    return result;
}

DbStatus Database::insert_node(const NodeRecord& node) noexcept
{
    if (!handle_ || !id_valid(node.node_id) || !node.parent_id || !id_valid(*node.parent_id) ||
        node.display_name.empty() || node.display_name.size() > 255 ||
        node.sibling_order > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        node.created_ts > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        return InvalidArgument;
    if (!parent_depth_allowed(handle_, *node.parent_id)) return Constraint;
    Statement statement{handle_,
                        "INSERT INTO "
                        "nodes(node_id,parent_id,node_type,display_name,sibling_order,favorite,"
                        "created_ts,media_format,width,height,duration_ms,codec,original_size,sort_"
                        "key,animated) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"};
    if (!statement.get()) return map_status(sqlite3_extended_errcode(handle_));
    bind_id(statement.get(), 1, node.node_id);
    bind_id(statement.get(), 2, *node.parent_id);
    sqlite3_bind_int(statement.get(), 3, static_cast<int>(std::to_underlying(node.type)));
    sqlite3_bind_text(statement.get(), 4, node.display_name.view().data(),
                      static_cast<int>(node.display_name.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement.get(), 5, static_cast<sqlite3_int64>(node.sibling_order));
    sqlite3_bind_int(statement.get(), 6, node.favorite ? 1 : 0);
    sqlite3_bind_int64(statement.get(), 7, static_cast<sqlite3_int64>(node.created_ts));
    const auto bind_optional = [&](int index, const auto& value) {
        if (value)
            sqlite3_bind_int64(statement.get(), index, static_cast<sqlite3_int64>(*value));
        else
            sqlite3_bind_null(statement.get(), index);
    };
    bind_optional(8, node.media_format);
    bind_optional(9, node.width);
    bind_optional(10, node.height);
    bind_optional(11, node.duration_ms);
    bind_optional(12, node.codec);
    bind_optional(13, node.original_size);
    bind_optional(14, node.sort_key);
    bind_optional(15, node.animated);
    return map_status(sqlite3_step(statement.get()));
}

DbResult<std::vector<NodeRecord>> Database::list_children(const Id& parent_id) const noexcept
{
    DbResult<std::vector<NodeRecord>> result{Ok, {}};
    if (!handle_ || !id_valid(parent_id)) return {InvalidArgument, {}};
    const std::string sql = std::string{"SELECT "} + NODE_COLUMNS +
                            " FROM nodes WHERE parent_id=? ORDER BY sibling_order";
    Statement statement{handle_, sql.c_str()};
    if (!statement.get()) return {map_status(sqlite3_extended_errcode(handle_)), {}};
    bind_id(statement.get(), 1, parent_id);
    for (;;) {
        const int step = sqlite3_step(statement.get());
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) return {map_status(step), {}};
        auto node = read_node(statement.get());
        if (!node) return {WrongKeyOrCorrupt, {}};
        result.value.push_back(std::move(*node));
    }
    return result;
}

DbResult<std::optional<NodeRecord>> Database::find_node(const Id& node_id) const noexcept
{
    if (!handle_ || !id_valid(node_id)) return {InvalidArgument, std::nullopt};
    const std::string sql = std::string{"SELECT "} + NODE_COLUMNS + " FROM nodes WHERE node_id=?";
    Statement statement{handle_, sql.c_str()};
    if (!statement.get()) return {map_status(sqlite3_extended_errcode(handle_)), std::nullopt};
    bind_id(statement.get(), 1, node_id);
    const int step = sqlite3_step(statement.get());
    if (step == SQLITE_DONE) return {Ok, std::nullopt};
    if (step != SQLITE_ROW) return {map_status(step), std::nullopt};
    auto node = read_node(statement.get());
    if (!node) return {WrongKeyOrCorrupt, std::nullopt};
    return {Ok, std::move(node)};
}

DbResult<std::vector<NodeRecord>> Database::search_nodes(std::string_view term) const noexcept
{
    DbResult<std::vector<NodeRecord>> result{Ok, {}};
    if (!handle_ || term.size() > 255) return {InvalidArgument, {}};
    const std::string sql = std::string{"SELECT DISTINCT "} + NODE_COLUMNS_N +
                            " FROM nodes n LEFT JOIN node_tags nt ON nt.node_id=n.node_id "
                            "LEFT JOIN tags t ON t.tag_id=nt.tag_id "
                            "WHERE n.display_name LIKE '%'||?||'%' COLLATE NOCASE "
                            "OR t.display_name LIKE '%'||?||'%' COLLATE NOCASE "
                            "ORDER BY n.node_id";
    Statement statement{handle_, sql.c_str()};
    if (!statement.get()) return {map_status(sqlite3_extended_errcode(handle_)), {}};
    sqlite3_bind_text(statement.get(), 1, term.data(), static_cast<int>(term.size()),
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(statement.get(), 2, term.data(), static_cast<int>(term.size()),
                      SQLITE_TRANSIENT);
    for (;;) {
        const int step = sqlite3_step(statement.get());
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) return {map_status(step), {}};
        auto node = read_node(statement.get());
        if (!node) return {WrongKeyOrCorrupt, {}};
        result.value.push_back(std::move(*node));
    }
    return result;
}

DbStatus Database::insert_object(const ObjectRecord& object) noexcept
{
    if (!handle_ || !id_valid(object.object_id) || !id_valid(object.node_id) ||
        object.encrypted_length == 0 || object.frame_plain_limit == 0 ||
        object.frame_plain_limit > 1048576 || object.frame_count == 0)
        return InvalidArgument;
    Statement statement{handle_,
                        "INSERT INTO "
                        "objects(object_id,node_id,role,encrypted_length,plaintext_length,frame_"
                        "plain_limit,frame_count,creation_generation) VALUES(?,?,?,?,?,?,?,?)"};
    if (!statement.get()) return map_status(sqlite3_extended_errcode(handle_));
    bind_id(statement.get(), 1, object.object_id);
    bind_id(statement.get(), 2, object.node_id);
    sqlite3_bind_int(statement.get(), 3, static_cast<int>(std::to_underlying(object.role)));
    sqlite3_bind_int64(statement.get(), 4, static_cast<sqlite3_int64>(object.encrypted_length));
    sqlite3_bind_int64(statement.get(), 5, static_cast<sqlite3_int64>(object.plaintext_length));
    sqlite3_bind_int(statement.get(), 6, static_cast<int>(object.frame_plain_limit));
    sqlite3_bind_int(statement.get(), 7, static_cast<int>(object.frame_count));
    sqlite3_bind_int64(statement.get(), 8, static_cast<sqlite3_int64>(object.creation_generation));
    return map_status(sqlite3_step(statement.get()));
}

DbResult<std::vector<ObjectRecord>> Database::object_references() const noexcept
{
    DbResult<std::vector<ObjectRecord>> result{Ok, {}};
    Statement statement{handle_,
                        "SELECT "
                        "object_id,node_id,role,encrypted_length,plaintext_length,frame_plain_"
                        "limit,frame_count,creation_generation FROM objects ORDER BY object_id"};
    if (!statement.get()) return {map_status(sqlite3_extended_errcode(handle_)), {}};
    for (;;) {
        const int step = sqlite3_step(statement.get());
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) return {map_status(step), {}};
        const auto object_id = column_id(statement.get(), 0);
        const auto node_id = column_id(statement.get(), 1);
        if (!object_id || !node_id) return {WrongKeyOrCorrupt, {}};
        const auto role = optional_integer<uint8_t>(statement.get(), 2);
        const auto encrypted_length = optional_integer<uint64_t>(statement.get(), 3);
        const auto plaintext_length = optional_integer<uint64_t>(statement.get(), 4);
        const auto frame_plain_limit = optional_integer<uint32_t>(statement.get(), 5);
        const auto frame_count = optional_integer<uint32_t>(statement.get(), 6);
        const auto generation = optional_integer<uint64_t>(statement.get(), 7);
        if (!role.has_value() || *role < std::to_underlying(ObjectRole::OriginalImage) ||
            *role > std::to_underlying(ObjectRole::OriginalVideo) ||
            !encrypted_length.has_value() || *encrypted_length == 0 ||
            !plaintext_length.has_value() || !frame_plain_limit.has_value() ||
            *frame_plain_limit == 0 || *frame_plain_limit > 1048576 || !frame_count.has_value() ||
            *frame_count == 0 || *frame_count > 1048576 || !generation.has_value())
            return {WrongKeyOrCorrupt, {}};
        ObjectRecord object{*object_id,
                            *node_id,
                            static_cast<ObjectRole>(*role),
                            *encrypted_length,
                            *plaintext_length,
                            *frame_plain_limit,
                            *frame_count,
                            *generation};
        if (result.value.size() >= 3'000'000) return {WrongKeyOrCorrupt, {}};
        result.value.emplace_back(std::move(object));
    }
    return result;
}

DbStatus Database::commit_object_create(ObjectRecord object) noexcept
{
    if (!handle_) return InvalidArgument;
    auto status = exec(handle_, "BEGIN IMMEDIATE");
    if (status != Ok) return status;
    const auto current = settings();
    if (current.status != Ok || current.value.app_generation ==
                                    static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        exec(handle_, "ROLLBACK");
        return current.status == Ok ? Constraint : current.status;
    }
    object.creation_generation = current.value.app_generation + 1;
    status = insert_object(object);
    if (status == Ok) {
        Statement generation{handle_, "UPDATE vault_meta SET app_generation=? WHERE singleton=1"};
        if (!generation.get())
            status = map_status(sqlite3_extended_errcode(handle_));
        else {
            sqlite3_bind_int64(generation.get(), 1,
                               static_cast<sqlite3_int64>(object.creation_generation));
            status = map_status(sqlite3_step(generation.get()));
        }
    }
    if (status == Ok) status = exec(handle_, "COMMIT");
    if (status != Ok) exec(handle_, "ROLLBACK");
    return status;
}

DbResult<std::optional<Id>> Database::commit_object_replace(ObjectRecord object) noexcept
{
    if (!handle_ || !id_valid(object.node_id)) return {InvalidArgument, std::nullopt};
    auto status = exec(handle_, "BEGIN IMMEDIATE");
    if (status != Ok) return {status, std::nullopt};
    std::optional<Id> old;
    {
        Statement find{handle_, "SELECT object_id FROM objects WHERE node_id=? AND role=?"};
        if (!find.get())
            status = map_status(sqlite3_extended_errcode(handle_));
        else {
            bind_id(find.get(), 1, object.node_id);
            sqlite3_bind_int(find.get(), 2, static_cast<int>(std::to_underlying(object.role)));
            const int step = sqlite3_step(find.get());
            if (step == SQLITE_ROW)
                old = column_id(find.get(), 0);
            else if (step != SQLITE_DONE)
                status = map_status(step);
        }
    }
    const auto current = settings();
    if (status == Ok && current.status != Ok) status = current.status;
    if (status == Ok && current.value.app_generation ==
                            static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        status = Constraint;
    if (status == Ok && old) {
        Statement erase{handle_, "DELETE FROM objects WHERE object_id=?"};
        if (!erase.get())
            status = map_status(sqlite3_extended_errcode(handle_));
        else {
            bind_id(erase.get(), 1, *old);
            status = map_status(sqlite3_step(erase.get()));
        }
    }
    if (status == Ok) {
        object.creation_generation = current.value.app_generation + 1;
        status = insert_object(object);
    }
    if (status == Ok) {
        Statement generation{handle_, "UPDATE vault_meta SET app_generation=app_generation+1 "
                                      "WHERE singleton=1"};
        status = generation.get() ? map_status(sqlite3_step(generation.get()))
                                  : map_status(sqlite3_extended_errcode(handle_));
    }
    if (status == Ok) status = exec(handle_, "COMMIT");
    if (status != Ok) {
        exec(handle_, "ROLLBACK");
        return {status, std::nullopt};
    }
    return {Ok, old};
}

DbResult<std::optional<Id>> Database::commit_object_delete(const Id& node_id,
                                                            ObjectRole role) noexcept
{
    if (!handle_ || !id_valid(node_id)) return {InvalidArgument, std::nullopt};
    auto status = exec(handle_, "BEGIN IMMEDIATE");
    if (status != Ok) return {status, std::nullopt};
    std::optional<Id> old;
    {
        Statement find{handle_, "SELECT object_id FROM objects WHERE node_id=? AND role=?"};
        if (!find.get())
            status = map_status(sqlite3_extended_errcode(handle_));
        else {
            bind_id(find.get(), 1, node_id);
            sqlite3_bind_int(find.get(), 2, static_cast<int>(std::to_underlying(role)));
            const int step = sqlite3_step(find.get());
            if (step == SQLITE_ROW)
                old = column_id(find.get(), 0);
            else if (step != SQLITE_DONE)
                status = map_status(step);
        }
    }
    if (status == Ok && old) {
        Statement erase{handle_, "DELETE FROM objects WHERE object_id=?"};
        if (!erase.get())
            status = map_status(sqlite3_extended_errcode(handle_));
        else {
            bind_id(erase.get(), 1, *old);
            status = map_status(sqlite3_step(erase.get()));
        }
    }
    if (status == Ok && old) {
        Statement generation{handle_, "UPDATE vault_meta SET app_generation=app_generation+1 "
                                      "WHERE singleton=1"};
        status = generation.get() ? map_status(sqlite3_step(generation.get()))
                                  : map_status(sqlite3_extended_errcode(handle_));
    }
    if (status == Ok) status = exec(handle_, "COMMIT");
    if (status != Ok) {
        exec(handle_, "ROLLBACK");
        return {status, std::nullopt};
    }
    return {Ok, old};
}

DbResult<bool> Database::object_is_referenced(const Id& object_id) const noexcept
{
    if (!handle_ || !id_valid(object_id)) return {InvalidArgument, false};
    Statement statement{handle_, "SELECT 1 FROM objects WHERE object_id=?"};
    if (!statement.get()) return {map_status(sqlite3_extended_errcode(handle_)), false};
    bind_id(statement.get(), 1, object_id);
    const int step = sqlite3_step(statement.get());
    if (step == SQLITE_ROW) return {Ok, true};
    if (step == SQLITE_DONE) return {Ok, false};
    return {map_status(step), false};
}

DbStatus Database::add_tag(int64_t tag_id, std::string_view display_name,
                           std::string_view canonical_name) noexcept
{
    if (!handle_ || tag_id <= 0 || display_name.empty() || display_name.size() > 255 ||
        canonical_name.empty() || canonical_name.size() > 255)
        return InvalidArgument;
    if (!count_below(handle_, "SELECT count(*) FROM tags", 4096)) return Constraint;
    Statement statement{handle_,
                        "INSERT INTO tags(tag_id,display_name,canonical_name) VALUES(?,?,?)"};
    if (!statement.get()) return map_status(sqlite3_extended_errcode(handle_));
    sqlite3_bind_int64(statement.get(), 1, tag_id);
    sqlite3_bind_text(statement.get(), 2, display_name.data(),
                      static_cast<int>(display_name.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(statement.get(), 3, canonical_name.data(),
                      static_cast<int>(canonical_name.size()), SQLITE_TRANSIENT);
    return map_status(sqlite3_step(statement.get()));
}

DbStatus Database::assign_tag(const Id& node_id, int64_t tag_id) noexcept
{
    if (!handle_ || !id_valid(node_id) || tag_id <= 0) return InvalidArgument;
    Statement statement{handle_, "INSERT INTO node_tags(node_id,tag_id) VALUES(?,?)"};
    if (!statement.get()) return map_status(sqlite3_extended_errcode(handle_));
    bind_id(statement.get(), 1, node_id);
    sqlite3_bind_int64(statement.get(), 2, tag_id);
    return map_status(sqlite3_step(statement.get()));
}

DbResult<std::vector<crypto::SecureString>> Database::node_tags(const Id& node_id) const noexcept
{
    DbResult<std::vector<crypto::SecureString>> result{Ok, {}};
    if (!handle_ || !id_valid(node_id)) return {InvalidArgument, {}};
    Statement statement{handle_, "SELECT t.display_name FROM tags t JOIN node_tags nt ON "
                                 "nt.tag_id=t.tag_id WHERE nt.node_id=? ORDER BY t.canonical_name"};
    if (!statement.get()) return {map_status(sqlite3_extended_errcode(handle_)), {}};
    bind_id(statement.get(), 1, node_id);
    for (;;) {
        const int step = sqlite3_step(statement.get());
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) return {map_status(step), {}};
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 0));
        const int size = sqlite3_column_bytes(statement.get(), 0);
        if (!text || size < 1) return {WrongKeyOrCorrupt, {}};
        result.value.emplace_back(std::string_view{text, static_cast<size_t>(size)});
    }
    return result;
}

DbStatus Database::add_tag_category(const TagCategoryRecord& category) noexcept
{
    if (!handle_ || category.category_id <= 0 || category.display_name.empty() ||
        category.display_name.size() > 64 || category.swatch > 15 || category.fields.size() > 16)
        return InvalidArgument;
    if (!count_below(handle_, "SELECT count(*) FROM tag_categories", 256)) return Constraint;
    for (const auto& field : category.fields)
        if (field.empty() || field.size() > 64) return InvalidArgument;
    auto status = exec(handle_, "BEGIN IMMEDIATE");
    if (status != Ok) return status;
    {
        Statement insert{
            handle_, "INSERT INTO tag_categories(category_id,display_name,swatch) VALUES(?,?,?)"};
        if (!insert.get())
            status = map_status(sqlite3_extended_errcode(handle_));
        else {
            sqlite3_bind_int64(insert.get(), 1, category.category_id);
            sqlite3_bind_text(insert.get(), 2, category.display_name.view().data(),
                              static_cast<int>(category.display_name.size()), SQLITE_TRANSIENT);
            sqlite3_bind_int(insert.get(), 3, category.swatch);
            status = map_status(sqlite3_step(insert.get()));
        }
    }
    for (size_t i = 0; status == Ok && i < category.fields.size(); ++i) {
        Statement field{
            handle_,
            "INSERT INTO category_fields(category_id,field_order,display_name) VALUES(?,?,?)"};
        if (!field.get()) {
            status = map_status(sqlite3_extended_errcode(handle_));
            break;
        }
        sqlite3_bind_int64(field.get(), 1, category.category_id);
        sqlite3_bind_int64(field.get(), 2, static_cast<sqlite3_int64>(i));
        sqlite3_bind_text(field.get(), 3, category.fields[i].view().data(),
                          static_cast<int>(category.fields[i].size()), SQLITE_TRANSIENT);
        status = map_status(sqlite3_step(field.get()));
    }
    if (status == Ok) status = exec(handle_, "COMMIT");
    if (status != Ok) exec(handle_, "ROLLBACK");
    return status;
}

DbResult<std::vector<TagCategoryRecord>> Database::tag_categories() const noexcept
{
    DbResult<std::vector<TagCategoryRecord>> result{Ok, {}};
    Statement categories{
        handle_, "SELECT category_id,display_name,swatch FROM tag_categories ORDER BY category_id"};
    if (!categories.get()) return {map_status(sqlite3_extended_errcode(handle_)), {}};
    for (;;) {
        const int step = sqlite3_step(categories.get());
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) return {map_status(step), {}};
        const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(categories.get(), 1));
        const int size = sqlite3_column_bytes(categories.get(), 1);
        if (!name || size < 1) return {WrongKeyOrCorrupt, {}};
        TagCategoryRecord category;
        category.category_id = sqlite3_column_int64(categories.get(), 0);
        category.display_name = std::string_view{name, static_cast<size_t>(size)};
        category.swatch = static_cast<uint8_t>(sqlite3_column_int(categories.get(), 2));
        Statement fields{
            handle_,
            "SELECT display_name FROM category_fields WHERE category_id=? ORDER BY field_order"};
        if (!fields.get()) return {map_status(sqlite3_extended_errcode(handle_)), {}};
        sqlite3_bind_int64(fields.get(), 1, category.category_id);
        while (sqlite3_step(fields.get()) == SQLITE_ROW) {
            const auto* field = reinterpret_cast<const char*>(sqlite3_column_text(fields.get(), 0));
            const int field_size = sqlite3_column_bytes(fields.get(), 0);
            if (!field || field_size < 1) return {WrongKeyOrCorrupt, {}};
            category.fields.emplace_back(std::string_view{field, static_cast<size_t>(field_size)});
        }
        result.value.push_back(std::move(category));
    }
    return result;
}

DbStatus Database::set_tag_field_value(const TagFieldValueRecord& value) noexcept
{
    if (!handle_ || value.tag_id <= 0 || value.category_id <= 0 || value.field_order > 15 ||
        value.value.size() > 256)
        return InvalidArgument;
    if (!count_below(handle_, "SELECT count(*) FROM tag_field_values", 16384)) return Constraint;
    Statement statement{
        handle_,
        "INSERT INTO tag_field_values(tag_id,category_id,field_order,value) VALUES(?,?,?,?) ON "
        "CONFLICT(tag_id,category_id,field_order) DO UPDATE SET value=excluded.value"};
    if (!statement.get()) return map_status(sqlite3_extended_errcode(handle_));
    sqlite3_bind_int64(statement.get(), 1, value.tag_id);
    sqlite3_bind_int64(statement.get(), 2, value.category_id);
    sqlite3_bind_int64(statement.get(), 3, value.field_order);
    sqlite3_bind_text(statement.get(), 4, value.value.view().data(),
                      static_cast<int>(value.value.size()), SQLITE_TRANSIENT);
    return map_status(sqlite3_step(statement.get()));
}

DbResult<std::vector<TagFieldValueRecord>> Database::tag_field_values(int64_t tag_id) const noexcept
{
    DbResult<std::vector<TagFieldValueRecord>> result{Ok, {}};
    if (!handle_ || tag_id <= 0) return {InvalidArgument, {}};
    Statement statement{handle_, "SELECT category_id,field_order,value FROM tag_field_values WHERE "
                                 "tag_id=? ORDER BY category_id,field_order"};
    if (!statement.get()) return {map_status(sqlite3_extended_errcode(handle_)), {}};
    sqlite3_bind_int64(statement.get(), 1, tag_id);
    for (;;) {
        const int step = sqlite3_step(statement.get());
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) return {map_status(step), {}};
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 2));
        const int size = sqlite3_column_bytes(statement.get(), 2);
        if (!text && size != 0) return {WrongKeyOrCorrupt, {}};
        TagFieldValueRecord value;
        value.tag_id = tag_id;
        value.category_id = sqlite3_column_int64(statement.get(), 0);
        value.field_order = static_cast<uint32_t>(sqlite3_column_int(statement.get(), 1));
        value.value = std::string_view{text ? text : "", static_cast<size_t>(size)};
        result.value.push_back(std::move(value));
    }
    return result;
}

DbStatus Database::set_tag_description(int64_t tag_id, std::string_view description) noexcept
{
    if (!handle_ || tag_id <= 0 || description.size() > 512) return InvalidArgument;
    Statement statement{handle_, "INSERT INTO tag_descriptions(tag_id,description) VALUES(?,?) ON "
                                 "CONFLICT(tag_id) DO UPDATE SET description=excluded.description"};
    if (!statement.get()) return map_status(sqlite3_extended_errcode(handle_));
    sqlite3_bind_int64(statement.get(), 1, tag_id);
    sqlite3_bind_text(statement.get(), 2, description.data(), static_cast<int>(description.size()),
                      SQLITE_TRANSIENT);
    return map_status(sqlite3_step(statement.get()));
}

DbResult<std::optional<crypto::SecureString>>
Database::tag_description(int64_t tag_id) const noexcept
{
    if (!handle_ || tag_id <= 0) return {InvalidArgument, std::nullopt};
    Statement statement{handle_, "SELECT description FROM tag_descriptions WHERE tag_id=?"};
    if (!statement.get()) return {map_status(sqlite3_extended_errcode(handle_)), std::nullopt};
    sqlite3_bind_int64(statement.get(), 1, tag_id);
    const int step = sqlite3_step(statement.get());
    if (step == SQLITE_DONE) return {Ok, std::nullopt};
    if (step != SQLITE_ROW) return {map_status(step), std::nullopt};
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 0));
    const int size = sqlite3_column_bytes(statement.get(), 0);
    if (!text && size != 0) return {WrongKeyOrCorrupt, std::nullopt};
    return {Ok,
            crypto::SecureString{std::string_view{text ? text : "", static_cast<size_t>(size)}}};
}

DbResult<std::vector<TagDescriptionRecord>> Database::tag_descriptions() const noexcept
{
    DbResult<std::vector<TagDescriptionRecord>> result{Ok, {}};
    Statement statement{handle_, "SELECT t.display_name,d.description FROM tag_descriptions d "
                                 "JOIN tags t ON t.tag_id=d.tag_id ORDER BY t.canonical_name"};
    if (!statement.get()) return {map_status(sqlite3_extended_errcode(handle_)), {}};
    for (;;) {
        const int step = sqlite3_step(statement.get());
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) return {map_status(step), {}};
        std::array<crypto::SecureString, 2> values;
        for (int column = 0; column < 2; ++column) {
            const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), column));
            const int size = sqlite3_column_bytes(statement.get(), column);
            if ((!text && size != 0) || size < (column == 0 ? 1 : 0))
                return {WrongKeyOrCorrupt, {}};
            values[static_cast<size_t>(column)] =
                crypto::SecureString(std::string_view{text ? text : "", static_cast<size_t>(size)});
        }
        result.value.emplace_back(std::move(values[0]), std::move(values[1]));
    }
    return result;
}

DbResult<std::vector<ResolvedTagFieldValueRecord>>
Database::resolved_tag_field_values() const noexcept
{
    DbResult<std::vector<ResolvedTagFieldValueRecord>> result{Ok, {}};
    Statement statement{handle_, "SELECT t.display_name,f.display_name,v.value "
                                 "FROM tag_field_values v JOIN tags t ON t.tag_id=v.tag_id "
                                 "JOIN category_fields f ON f.category_id=v.category_id AND "
                                 "f.field_order=v.field_order ORDER BY t.canonical_name,"
                                 "v.category_id,v.field_order"};
    if (!statement.get()) return {map_status(sqlite3_extended_errcode(handle_)), {}};
    for (;;) {
        const int step = sqlite3_step(statement.get());
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) return {map_status(step), {}};
        std::array<crypto::SecureString, 3> values;
        for (int column = 0; column < 3; ++column) {
            const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), column));
            const int size = sqlite3_column_bytes(statement.get(), column);
            if ((!text && size != 0) || size < (column < 2 ? 1 : 0))
                return {WrongKeyOrCorrupt, {}};
            values[static_cast<size_t>(column)] =
                crypto::SecureString(std::string_view{text ? text : "", static_cast<size_t>(size)});
        }
        result.value.emplace_back(std::move(values[0]), std::move(values[1]), std::move(values[2]));
    }
    return result;
}

DbStatus Database::set_settings(const SettingsRecord& settings) noexcept
{
    if (!handle_ || settings.default_sort > 7 || settings.migrated_index_version > 13)
        return InvalidArgument;
    Statement statement{handle_,
                        "UPDATE vault_meta SET "
                        "app_generation=?,default_sort=?,tiles_show_tags=?,migrated_index_version=?"
                        ",migrated_probe_caps=?,migrated_thumb_side=? WHERE singleton=1"};
    if (!statement.get()) return map_status(sqlite3_extended_errcode(handle_));
    sqlite3_bind_int64(statement.get(), 1, static_cast<sqlite3_int64>(settings.app_generation));
    sqlite3_bind_int(statement.get(), 2, settings.default_sort);
    sqlite3_bind_int(statement.get(), 3, settings.tiles_show_tags ? 1 : 0);
    sqlite3_bind_int(statement.get(), 4, settings.migrated_index_version);
    sqlite3_bind_int(statement.get(), 5, settings.migrated_probe_caps);
    sqlite3_bind_int(statement.get(), 6, settings.migrated_thumb_side);
    if (const int step = sqlite3_step(statement.get()); step != SQLITE_DONE)
        return map_status(step);
    return sqlite3_changes(handle_) == 1 ? Ok : WrongKeyOrCorrupt;
}

DbResult<SettingsRecord> Database::settings() const noexcept
{
    Statement statement{
        handle_, "SELECT "
                 "app_generation,default_sort,tiles_show_tags,migrated_index_version,migrated_"
                 "probe_caps,migrated_thumb_side FROM vault_meta WHERE singleton=1"};
    if (!statement.get()) return {map_status(sqlite3_extended_errcode(handle_)), {}};
    if (sqlite3_step(statement.get()) != SQLITE_ROW) return {WrongKeyOrCorrupt, {}};
    SettingsRecord result{static_cast<uint64_t>(sqlite3_column_int64(statement.get(), 0)),
                          static_cast<uint8_t>(sqlite3_column_int(statement.get(), 1)),
                          sqlite3_column_int(statement.get(), 2) != 0,
                          static_cast<uint8_t>(sqlite3_column_int(statement.get(), 3)),
                          static_cast<uint16_t>(sqlite3_column_int(statement.get(), 4)),
                          static_cast<uint16_t>(sqlite3_column_int(statement.get(), 5))};
    return {Ok, result};
}

DbStatus Database::add_saved_search(const SavedSearchRecord& search) noexcept
{
    if (!handle_ || search.search_id <= 0 || search.display_name.empty() ||
        search.display_name.size() > 255 || search.query.size() > 1048576)
        return InvalidArgument;
    if (!count_below(handle_, "SELECT count(*) FROM saved_searches", 4096)) return Constraint;
    Statement statement{handle_,
                        "INSERT INTO saved_searches(search_id,display_name,query) VALUES(?,?,?)"};
    if (!statement.get()) return map_status(sqlite3_extended_errcode(handle_));
    sqlite3_bind_int64(statement.get(), 1, search.search_id);
    sqlite3_bind_text(statement.get(), 2, search.display_name.view().data(),
                      static_cast<int>(search.display_name.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob(statement.get(), 3, search.query.data(),
                      static_cast<int>(search.query.size()), SQLITE_TRANSIENT);
    return map_status(sqlite3_step(statement.get()));
}

DbResult<std::vector<SavedSearchRecord>> Database::saved_searches() const noexcept
{
    DbResult<std::vector<SavedSearchRecord>> result{Ok, {}};
    Statement statement{
        handle_, "SELECT search_id,display_name,query FROM saved_searches ORDER BY search_id"};
    if (!statement.get()) return {map_status(sqlite3_extended_errcode(handle_)), {}};
    for (;;) {
        const int step = sqlite3_step(statement.get());
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) return {map_status(step), {}};
        const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 1));
        const auto* query = static_cast<const uint8_t*>(sqlite3_column_blob(statement.get(), 2));
        const int name_size = sqlite3_column_bytes(statement.get(), 1);
        const int query_size = sqlite3_column_bytes(statement.get(), 2);
        if (!name || name_size < 1 || query_size < 0 || (query_size > 0 && !query))
            return {WrongKeyOrCorrupt, {}};
        SavedSearchRecord item;
        item.search_id = sqlite3_column_int64(statement.get(), 0);
        item.display_name = std::string_view{name, static_cast<size_t>(name_size)};
        if (!item.query.assign({query, static_cast<size_t>(query_size)})) return {IoError, {}};
        result.value.push_back(std::move(item));
    }
    return result;
}

DbStatus Database::backup_to(const std::filesystem::path& destination,
                             std::span<const uint8_t, crypto::KEY_SIZE> database_key) const noexcept
{
    if (!handle_) return InvalidArgument;
    if (std::error_code path_error; std::filesystem::exists(destination, path_error) || path_error)
        return IoError;
    std::array<uint8_t, 8> random{};
    if (!crypto::fill_random(random)) return IoError;
    std::string suffix;
    suffix.reserve(16);
    for (const auto byte : random)
        suffix += std::format("{:02x}", byte);
    const auto parent =
        destination.parent_path().empty() ? std::filesystem::path{"."} : destination.parent_path();
    const auto temporary = parent / (destination.filename().string() + ".partial-" + suffix);
    const int claimed = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (claimed < 0) return IoError;
    ::close(claimed);
    const auto remove_temporary = [&] {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    };
    auto target = open_raw(temporary, database_key, SQLITE_OPEN_READWRITE);
    if (!target.database) {
        remove_temporary();
        return target.status;
    }
    if (const auto status = copy_database(handle_, target.database->handle_);
        status != Ok || !database_healthy(*target.database)) {
        target.database.reset();
        remove_temporary();
        return status;
    }
    target.database.reset();
    if (!publish_backup(temporary, destination, parent)) {
        remove_temporary();
        return IoError;
    }
    return Ok;
}

DbStatus set_database_user_version_for_test(Database& database, int version) noexcept
{
    if (!database.handle_ || version < 0) return InvalidArgument;
    return exec(database.handle_, std::format("PRAGMA user_version={}", version));
}

}  // namespace vault::v3
