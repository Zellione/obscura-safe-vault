#include "test_framework.h"

#include "vault/staging.h"
#include "vault/v3_db.h"
#include "vault/v3_fs.h"
#include "vault/v3_object_store.h"
#include "vault/v3_recovery.h"
#include "vault/vault.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <print>
#include <unistd.h>

namespace {
namespace fs = std::filesystem;
using namespace vault;
using namespace vault::v3;
using Clock = std::chrono::steady_clock;

constexpr Id ROOT{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
constexpr Id VAULT{3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3};
constexpr std::array<uint8_t, crypto::KEY_SIZE> KEY{1, 2, 3, 4, 5, 6, 7, 8};

struct TempDir {
    fs::path path;
    TempDir()
    {
        std::array<char, 64> pattern{};
        std::snprintf(pattern.data(), pattern.size(), "/tmp/osv-v3-bench-XXXXXX");
        if (const char* made = ::mkdtemp(pattern.data())) path = made;
    }
    ~TempDir()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

Id node_id(size_t value)
{
    Id id{};
    id[0] = 0xb4;
    for (size_t i = 0; i < sizeof(value) && i + 1 < id.size(); ++i)
        id[i + 1] = static_cast<uint8_t>(value >> (i * 8));
    return id;
}

double elapsed_ms(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

bool within(double actual, double fixed_ms, double per_node_ms, size_t nodes)
{
    return actual <= fixed_ms + per_node_ms * static_cast<double>(nodes);
}
}  // namespace

// Opt-in because the 250k case is deliberately a workstation-scale regression gate:
//   OSV_V3_BENCH_SCALE=1000|50000|250000 build/bin/Release/osv_tests v3_scale_benchmark
TEST(v3_scale_benchmark_measures_sql_repository_operations)
{
    const char* configured = std::getenv("OSV_V3_BENCH_SCALE");
    if (!configured) return;
    const size_t count = static_cast<size_t>(std::strtoull(configured, nullptr, 10));
    REQUIRE(count == 1000 || count == 50000 || count == 250000);

    TempDir temp;
    auto root_handle = VaultRoot::create(temp.path / "source.osv");
    REQUIRE(root_handle.has_value());
    auto writer = root_handle->try_writer_lock();
    REQUIRE(writer.has_value());
    auto opened = Database::create_in_root(*root_handle, KEY, ROOT);
    REQUIRE(opened.database.has_value());

    IndexNode tree = IndexNode::gallery("/");
    tree.node_id = ROOT;
    tree.children.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        auto child = IndexNode::image("image-" + std::to_string(i) + ".jpg");
        child.node_id = node_id(i + 1);
        child.meta.format = ImageFormat::JPEG;
        child.meta.orig_size = 4096;
        if (i % 100 == 0) child.tags.emplace_back("benchmark");
        tree.children.push_back(std::move(child));
    }
    VaultSettings settings;

    auto start = Clock::now();
    REQUIRE(opened.database->sync_metadata(tree, settings, {}) == DbStatus::Ok);
    const double import_ms = elapsed_ms(start);
    CHECK(within(import_ms, 1000, 0.20, count));

    start = Clock::now();
    const auto listing = opened.database->list_children(ROOT);
    const double list_ms = elapsed_ms(start);
    REQUIRE(listing.status == DbStatus::Ok);
    CHECK_EQ(listing.value.size(), count);
    CHECK(within(list_ms, 250, 0.08, count));

    start = Clock::now();
    const auto search = opened.database->search_nodes("image-999");
    const double search_ms = elapsed_ms(start);
    REQUIRE(search.status == DbStatus::Ok);
    CHECK(within(search_ms, 250, 0.04, count));

    start = Clock::now();
    const auto tags = opened.database->node_tags(node_id(1));
    const double tag_ms = elapsed_ms(start);
    REQUIRE(tags.status == DbStatus::Ok);
    CHECK(within(tag_ms, 100, 0, count));

    const ObjectWriteRequest request{VAULT, node_id(1), ObjectRole::OriginalVideo, 1, 64 * 1024};
    crypto::SecureBytes media;
    REQUIRE(media.resize(1024 * 1024));
    const auto written = write_object(*root_handle, KEY, request, media.as_span());
    REQUIRE(written.status == ObjectStatus::Ok);
    auto reader = ObjectReader::open(*root_handle, KEY, written.info);
    REQUIRE(reader.status == ObjectStatus::Ok);
    start = Clock::now();
    for (size_t i = 0; i < 100; ++i) {
        crypto::SecureBytes range;
        REQUIRE(reader.reader->read_range((i * 7919) % (media.size() - 4096), 4096, range) ==
                ObjectStatus::Ok);
    }
    const double seek_ms = elapsed_ms(start);
    CHECK(seek_ms <= 1000);

    start = Clock::now();
    const auto verification =
        verify_vault(*root_handle, *opened.database, KEY, VAULT, VerifyDepth::Quick);
    const double verify_ms = elapsed_ms(start);
    // This SQL-only fixture deliberately has no original-object references.
    REQUIRE(verification.status == RecoveryStatus::Corrupt);
    CHECK(verification.findings.size() == count + 1);
    CHECK(within(verify_ms, 500, 0.02, count));

    start = Clock::now();
    const auto reconciliation = reconcile_objects(*root_handle, *opened.database);
    REQUIRE(reconciliation.status == RecoveryStatus::Ok);
    const auto gc =
        collect_garbage(*root_handle, *writer, *opened.database, reconciliation.garbage, 0);
    const double gc_ms = elapsed_ms(start);
    REQUIRE(gc.status == RecoveryStatus::Ok);
    CHECK(within(gc_ms, 500, 0.02, count));

    start = Clock::now();
    REQUIRE(opened.database->backup_to(temp.path / "snapshot.db", KEY) == DbStatus::Ok);
    const double backup_ms = elapsed_ms(start);
    CHECK(within(backup_ms, 1000, 0.08, count));

    tree.children.resize(count / 2);
    start = Clock::now();
    REQUIRE(opened.database->sync_metadata(tree, settings, {}) == DbStatus::Ok);
    const double delete_ms = elapsed_ms(start);
    CHECK(within(delete_ms, 1000, 0.20, count));

    start = Clock::now();
    opened.database.reset();
    const double shutdown_ms = elapsed_ms(start);
    CHECK(shutdown_ms <= 500);

    std::println("  [V3 BENCH] nodes={} import={:.1f}ms list={:.1f}ms search={:.1f}ms "
                 "tag={:.1f}ms seek100={:.1f}ms verify={:.1f}ms gc={:.1f}ms backup={:.1f}ms "
                 "delete50%={:.1f}ms shutdown={:.1f}ms",
                 count, import_ms, list_ms, search_ms, tag_ms, seek_ms, verify_ms, gc_ms, backup_ms,
                 delete_ms, shutdown_ms);
}

// Real facade, two encrypted objects per media, batched import, cold unlock,
// and repeated scattered thumbnail reads. The SQL-only microbenchmark above
// does not measure these production paths.
TEST(v3_facade_benchmark_with_representative_object_counts)
{
    const char* configured = std::getenv("OSV_V3_FACADE_BENCH");
    if (!configured) return;
    const size_t count = static_cast<size_t>(std::strtoull(configured, nullptr, 10));
    REQUIRE(count >= 1000 && count <= 250000);
    TempDir temp;
    Vault vault;
    constexpr std::array<uint8_t, 2> password{'p', 'w'};
    REQUIRE(Vault::create_directory((temp.path / "facade.osv").string(), password, {}, {1, 8, 1},
                                    vault) == VaultResult::Ok);
    const std::array<uint8_t, 4> bytes{1, 2, 3, 4};
    StagedThumb thumb;
    REQUIRE(thumb.thumb_jpeg.assign(bytes));
    auto start = Clock::now();
    for (size_t i = 0; i < count; ++i) {
        auto staged = stage_image(vault, bytes, "item-" + std::to_string(i), &thumb);
        REQUIRE(staged.status == VaultResult::Ok);
        REQUIRE(attach_staged(vault, "", std::move(staged.node)) == VaultResult::Ok);
        if ((i + 1) % 1000 == 0) REQUIRE(commit_staged(vault) == VaultResult::Ok);
    }
    REQUIRE(commit_staged(vault) == VaultResult::Ok);
    const auto import_ms = elapsed_ms(start);
    vault.lock();
    start = Clock::now();
    REQUIRE(vault.unlock(password, {}) == VaultResult::Ok);
    const auto unlock_ms = elapsed_ms(start);
    const auto children = vault.list("");
    REQUIRE(children.size() == count);
    start = Clock::now();
    crypto::SecureBytes read;
    for (size_t i = 0; i < 2000; ++i) {
        const auto* node = children[(i * 7919) % count];
        REQUIRE(read_thumb_span(vault, media_thumb_chunk_ref(*node), read) == VaultResult::Ok);
        REQUIRE(std::ranges::equal(read.as_span(), bytes));
    }
    const auto thumbnails_ms = elapsed_ms(start);
    std::println("  [V3 FACADE] nodes={} objects={} import={:.1f}ms unlock={:.1f}ms "
                 "2000-thumbnails={:.1f}ms",
                 count, 2 * count, import_ms, unlock_ms, thumbnails_ms);
    CHECK(within(unlock_ms, 2000, 2, count));
    CHECK(thumbnails_ms < 10000);
    vault.lock();
}
