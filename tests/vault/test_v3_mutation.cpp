#include "test_framework.h"

#include "vault/v3_mutation.h"
#include "vault/v3_recovery.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <unistd.h>

namespace {
namespace fs = std::filesystem;
using namespace vault::v3;
constexpr std::array<uint8_t, 32> KEY{8, 7, 6, 5, 4, 3, 2, 1};
constexpr Id ROOT{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
constexpr Id NODE{2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
constexpr Id VAULT{3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3};
struct TempDir {
    fs::path path;
    TempDir()
    {
        std::array<char, 64> p{};
        std::snprintf(p.data(), p.size(), "/tmp/osv-v3-mutation-XXXXXX");
        if (const char* made = ::mkdtemp(p.data())) path = made;
    }
    ~TempDir() { std::error_code e; fs::remove_all(path, e); }
};
}  // namespace

TEST(v3_mutation_coordinator_publishes_before_reference_and_swaps_new_first)
{
    TempDir temp;
    auto root = VaultRoot::create(temp.path / "vault.osv");
    REQUIRE(root.has_value());
    auto opened = Database::create(temp.path / "db", KEY, ROOT);
    REQUIRE(opened.database.has_value());
    NodeRecord node;
    node.node_id = NODE;
    node.parent_id = ROOT;
    node.type = NodeType::Image;
    node.display_name = "x";
    REQUIRE(opened.database->insert_node(node) == DbStatus::Ok);
    auto coordinator = MutationCoordinator::open(*root, *opened.database, KEY);
    REQUIRE(coordinator.has_value());
    ObjectWriteRequest request{VAULT, NODE, ObjectRole::OriginalImage, 1, 1024};

    const auto first = coordinator->create(request, std::array<uint8_t, 3>{1, 2, 3});
    REQUIRE(first.status == MutationStatus::Ok);
    CHECK_TRUE(root->open_object(first.object_id).has_value());
    CHECK_TRUE(opened.database->object_is_referenced(first.object_id).value);

    const auto second = coordinator->replace(request, std::array<uint8_t, 2>{4, 5});
    REQUIRE(second.committed());
    CHECK_TRUE(root->open_object(second.object_id).has_value());
    CHECK_FALSE(root->open_object(first.object_id).has_value());
    CHECK_TRUE(opened.database->object_is_referenced(second.object_id).value);

    REQUIRE(coordinator->remove(NODE, ObjectRole::OriginalImage) == MutationStatus::Ok);
    CHECK_FALSE(root->open_object(second.object_id).has_value());
}

TEST(v3_mutation_publication_failure_never_creates_database_reference)
{
    TempDir temp;
    auto root = VaultRoot::create(temp.path / "vault.osv");
    REQUIRE(root.has_value());
    auto opened = Database::create(temp.path / "db", KEY, ROOT);
    REQUIRE(opened.database.has_value());
    NodeRecord node;
    node.node_id = NODE;
    node.parent_id = ROOT;
    node.type = NodeType::Image;
    node.display_name = "x";
    REQUIRE(opened.database->insert_node(node) == DbStatus::Ok);
    auto coordinator = MutationCoordinator::open(*root, *opened.database, KEY);
    REQUIRE(coordinator.has_value());
    inject_fs_fault(FsFault::Rename);
    ObjectWriteRequest request{VAULT, NODE, ObjectRole::OriginalImage, 1, 1024};
    const auto outcome = coordinator->create(request, std::array<uint8_t, 1>{9});
    clear_fs_faults();
    CHECK_TRUE(outcome.status == MutationStatus::ObjectWriteFailed);
    CHECK_TRUE(opened.database->object_references().value.empty());
}

TEST(v3_mutation_fault_matrix_cold_reopens_to_prior_or_new_state)
{
    for (const auto fault : {FsFault::Write, FsFault::FileSync, FsFault::Rename,
                             FsFault::DirectorySync}) {
        TempDir temp;
        auto root = VaultRoot::create(temp.path / "vault.osv");
        REQUIRE(root.has_value());
        const auto db_path = temp.path / "db";
        auto opened = Database::create(db_path, KEY, ROOT);
        REQUIRE(opened.database.has_value());
        NodeRecord node;
        node.node_id = NODE;
        node.parent_id = ROOT;
        node.type = NodeType::Image;
        node.display_name = "x";
        REQUIRE(opened.database->insert_node(node) == DbStatus::Ok);
        auto coordinator = MutationCoordinator::open(*root, *opened.database, KEY);
        REQUIRE(coordinator.has_value());
        inject_fs_fault(fault);
        ObjectWriteRequest request{VAULT, NODE, ObjectRole::OriginalImage, 1, 1024};
        CHECK_FALSE(coordinator->create(request, std::array<uint8_t, 3>{1, 2, 3}).committed());
        clear_fs_faults();
        coordinator.reset();
        opened.database.reset();

        auto cold = Database::open(db_path, KEY, false);
        REQUIRE(cold.database.has_value());
        CHECK_TRUE(cold.database->object_references().value.empty());
        const auto report = reconcile_objects(*root, *cold.database);
        CHECK_TRUE(report.status == RecoveryStatus::Ok);
        CHECK_TRUE(report.missing.empty());
    }
}

TEST(v3_mutation_database_failure_leaves_recoverable_garbage)
{
    TempDir temp;
    auto root = VaultRoot::create(temp.path / "vault.osv");
    REQUIRE(root.has_value());
    auto opened = Database::create(temp.path / "db", KEY, ROOT);
    REQUIRE(opened.database.has_value());
    auto coordinator = MutationCoordinator::open(*root, *opened.database, KEY);
    REQUIRE(coordinator.has_value());
    ObjectWriteRequest request{VAULT, NODE, ObjectRole::OriginalImage, 1, 1024};
    const auto outcome = coordinator->create(request, std::array<uint8_t, 1>{9});
    REQUIRE(outcome.status == MutationStatus::DatabaseFailed);
    const auto report = reconcile_objects(*root, *opened.database);
    REQUIRE(report.status == RecoveryStatus::Ok);
    CHECK_TRUE(report.missing.empty());
    REQUIRE(report.garbage.size() == 1);
    CHECK_EQ(report.garbage[0].id, outcome.object_id);
}
