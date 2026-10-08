#include "test_framework.h"

#include "vault/v3_backup.h"
#include "vault/v3_crypto_spec.h"
#include "vault/v3_object_store.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <unistd.h>

namespace {
namespace fs = std::filesystem;
using namespace vault::v3;
constexpr Id ROOT{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
constexpr Id NODE{2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
constexpr std::array<uint8_t, 2> PASSWORD{'p', 'w'};

struct TempDir {
    fs::path path;
    TempDir()
    {
        std::array<char, 64> pattern{};
        std::snprintf(pattern.data(), pattern.size(), "/tmp/osv-v3-backup-XXXXXX");
        if (const char* made = ::mkdtemp(pattern.data())) path = made;
    }
    ~TempDir()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};
}  // namespace

TEST(v3_backup_is_deep_verified_complete_and_published_no_replace)
{
    TempDir temp;
    auto source = VaultRoot::create(temp.path / "source.osv");
    REQUIRE(source.has_value());
    auto writer = source->try_writer_lock();
    REQUIRE(writer.has_value());
    V3Header header;
    crypto::SecureBuffer<crypto::KEY_SIZE> master;
    REQUIRE(create_v3_header(PASSWORD, {}, {1, 8, 1}, header, master) == HeaderStatus::Ok);
    REQUIRE(source->write_header(serialize_v3_header(header)));
    auto db_key = derive_database_key(master.as_span(), header.vault_id);
    auto opened = Database::create_in_root(*source, db_key.as_span(), ROOT);
    REQUIRE(opened.database.has_value());
    NodeRecord node;
    node.node_id = NODE;
    node.parent_id = ROOT;
    node.type = NodeType::Image;
    node.display_name = "secret.jpg";
    node.media_format = 1;
    node.original_size = 6;
    REQUIRE(opened.database->insert_node(node) == DbStatus::Ok);
    const std::array<uint8_t, 6> plaintext{1, 2, 3, 4, 5, 6};
    const ObjectWriteRequest request{.vault_id = header.vault_id,
                                     .owner_node_id = NODE,
                                     .role = ObjectRole::OriginalImage,
                                     .media_format = 1};
    const auto written = write_object(*source, master.as_span(), request, plaintext);
    REQUIRE(written.status == ObjectStatus::Ok);
    REQUIRE(opened.database->commit_object_create(
                {written.info.object_id, NODE, ObjectRole::OriginalImage,
                 written.info.encrypted_length, written.info.plaintext_length,
                 written.info.frame_plain_limit, written.info.frame_count, 0}) == DbStatus::Ok);

    const auto destination = temp.path / "snapshot.osv";
    const auto result = backup_vault(*source, *writer, *opened.database, db_key.as_span(),
                                     master.as_span(), header, destination);
    REQUIRE(result.status == BackupStatus::Ok);
    CHECK_EQ(result.copied_objects, 1);
    auto backup = VaultRoot::open(destination);
    REQUIRE(backup.has_value());
    auto backup_db = Database::open_read_only(*backup, db_key.as_span());
    REQUIRE(backup_db.database.has_value());
    CHECK(verify_vault(*backup, *backup_db.database, master.as_span(), header.vault_id,
                       VerifyDepth::Deep)
              .status == RecoveryStatus::Ok);

    CHECK(backup_vault(*source, *writer, *opened.database, db_key.as_span(), master.as_span(),
                       header, destination)
              .status == BackupStatus::AlreadyExists);

    const auto restored_path = temp.path / "restored.osv";
    const auto restored = restore_vault(destination, PASSWORD, {}, restored_path);
    REQUIRE(restored.status == BackupStatus::Ok);
    auto restored_root = VaultRoot::open(restored_path);
    REQUIRE(restored_root.has_value());
    auto restored_db = Database::open_read_only(*restored_root, db_key.as_span());
    REQUIRE(restored_db.database.has_value());
    CHECK(verify_vault(*restored_root, *restored_db.database, master.as_span(), header.vault_id,
                       VerifyDepth::Deep)
              .status == RecoveryStatus::Ok);

    // Restore is creation-only: a collision cannot overlay the verified destination or mutate
    // the source snapshot.
    CHECK(restore_vault(destination, PASSWORD, {}, restored_path).status ==
          BackupStatus::AlreadyExists);
    CHECK(verify_vault(*backup, *backup_db.database, master.as_span(), header.vault_id,
                       VerifyDepth::Deep)
              .status == RecoveryStatus::Ok);
}

TEST(v3_restore_rejects_incomplete_source_without_creating_destination)
{
    TempDir temp;
    const auto incomplete_path = temp.path / "incomplete.osv";
    auto incomplete = VaultRoot::create(incomplete_path);
    REQUIRE(incomplete.has_value());
    const auto destination = temp.path / "must-not-exist.osv";
    CHECK(restore_vault(incomplete_path, PASSWORD, {}, destination).status ==
          BackupStatus::SourceCorrupt);
    CHECK_FALSE(fs::exists(destination));
    CHECK_TRUE(incomplete->validate_layout());
}

TEST(v3_backup_preflight_uses_checked_math_and_database_journal_headroom)
{
    const auto required = backup_space_required(4096, 8192);
    REQUIRE(required.has_value());
    CHECK_EQ(*required, 4096U * 2U + 8192U + 1024U * 1024U);
    CHECK_FALSE(backup_space_required(UINT64_MAX, 1).has_value());
}
