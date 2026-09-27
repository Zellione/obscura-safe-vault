#include "test_framework.h"

#include "vault/v3_fs.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace vault::v3;

namespace {
struct TempDir {
    fs::path path;
    explicit TempDir(const char* tag)
    {
        static unsigned n = 0;
        path = fs::temp_directory_path() /
               ("osv_v3fs_" + std::string(tag) + "_" + std::to_string(n++));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

mode_t mode_of(const fs::path& p)
{
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 ? st.st_mode & 0777 : 0;
}

ObjectId id_from(uint8_t first)
{
    ObjectId id{};
    id[0] = first;
    for (size_t i = 1; i < id.size(); ++i)
        id[i] = static_cast<uint8_t>(i);
    return id;
}
}  // namespace

TEST(v3_fs_create_layout_is_owner_only_and_valid)
{
    TempDir d("create");
    const auto old = ::umask(0);
    auto root = VaultRoot::create(d.path / "album.osv");
    ::umask(old);
    REQUIRE(root.has_value());
    CHECK_EQ(mode_of(d.path / "album.osv"), 0700);
    CHECK_EQ(mode_of(d.path / "album.osv/objects"), 0700);
    CHECK_EQ(mode_of(d.path / "album.osv/staging"), 0700);
    CHECK_EQ(mode_of(d.path / "album.osv/vault.header"), 0600);
    CHECK_EQ(mode_of(d.path / "album.osv/vault.db"), 0600);
    CHECK_EQ(mode_of(d.path / "album.osv/lock"), 0600);
    CHECK_TRUE(root->validate_layout());
}

TEST(v3_fs_open_rejects_symlink_root_and_nested_links)
{
    TempDir d("links");
    auto root = VaultRoot::create(d.path / "real.osv");
    REQUIRE(root.has_value());
    std::error_code ec;
    fs::create_directory_symlink(d.path / "real.osv", d.path / "link.osv", ec);
    REQUIRE(!ec);
    CHECK_FALSE(VaultRoot::open(d.path / "link.osv").has_value());
    root.reset();
    fs::remove(d.path / "real.osv/vault.db", ec);
    fs::create_symlink("outside", d.path / "real.osv/vault.db", ec);
    CHECK_FALSE(VaultRoot::open(d.path / "real.osv").has_value());
}

TEST(v3_fs_rejects_unknown_types_bad_permissions_and_hardlinks)
{
    TempDir d("shape");
    auto root = VaultRoot::create(d.path / "a.osv");
    REQUIRE(root.has_value());
    {
        std::ofstream(d.path / "a.osv/surprise") << "x";
    }
    CHECK_FALSE(root->validate_layout());
    fs::remove(d.path / "a.osv/surprise");
    ::chmod((d.path / "a.osv/vault.db").c_str(), 0640);
    CHECK_FALSE(root->validate_layout());
    ::chmod((d.path / "a.osv/vault.db").c_str(), 0600);
    CHECK_EQ(::link((d.path / "a.osv/vault.db").c_str(), (d.path / "db-link").c_str()), 0);
    CHECK_FALSE(root->validate_layout());
}

TEST(v3_fs_object_name_grammar_is_canonical)
{
    const auto id = id_from(0xab);
    CHECK_TRUE(object_relative_path(id) == "objects/ab/ab0102030405060708090a0b0c0d0e0f.osvo");
    CHECK_TRUE(parse_object_relative_path("objects/ab/ab0102030405060708090a0b0c0d0e0f.osvo") ==
               id);
    CHECK_FALSE(
        parse_object_relative_path("objects/ac/ab0102030405060708090a0b0c0d0e0f.osvo").has_value());
    CHECK_FALSE(parse_object_relative_path("objects/ab/../../x.osvo").has_value());
    CHECK_FALSE(
        parse_object_relative_path("objects/AB/AB0102030405060708090A0B0C0D0E0F.osvo").has_value());
}

TEST(v3_fs_writer_lock_is_kernel_exclusive_and_raii_released)
{
    TempDir d("lock");
    auto root = VaultRoot::create(d.path / "a.osv");
    REQUIRE(root.has_value());
    auto first = root->try_writer_lock();
    REQUIRE(first.has_value());
    const pid_t child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        auto other = VaultRoot::open(d.path / "a.osv");
        _exit(other && other->try_writer_lock() ? 1 : 0);
    }
    int status = 0;
    REQUIRE(::waitpid(child, &status, 0) == child);
    CHECK_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    first.reset();
    CHECK_TRUE(root->try_writer_lock().has_value());
}

TEST(v3_fs_stage_write_sync_publish_and_unlink)
{
    TempDir d("publish");
    auto root = VaultRoot::create(d.path / "a.osv");
    REQUIRE(root.has_value());
    const auto id = id_from(0x2a);
    auto file = root->create_staging_file();
    REQUIRE(file.has_value());
    const std::array<uint8_t, 4> bytes{1, 2, 3, 4};
    CHECK_TRUE(file->write_all(bytes));
    CHECK_TRUE(file->sync());
    CHECK_TRUE(root->publish(*file, id));
    CHECK_TRUE(fs::exists(d.path / "a.osv" / object_relative_path(id)));
    CHECK_FALSE(root->publish(*file, id));
    CHECK_TRUE(root->unlink_object(id));
    CHECK_FALSE(fs::exists(d.path / "a.osv" / object_relative_path(id)));
}

TEST(v3_fs_parallel_publishers_cannot_claim_the_same_object)
{
    TempDir d("object_race");
    auto root = VaultRoot::create(d.path / "a.osv");
    REQUIRE(root.has_value());
    auto a = root->create_staging_file();
    auto b = root->create_staging_file();
    REQUIRE(a.has_value() && b.has_value());
    REQUIRE(a->sync() && b->sync());
    bool won_a = false, won_b = false;
    const auto id = id_from(0x4d);
    std::thread ta([&] { won_a = root->publish(*a, id); });
    std::thread tb([&] { won_b = root->publish(*b, id); });
    ta.join();
    tb.join();
    CHECK_TRUE(won_a != won_b);
    CHECK_TRUE(root->validate_layout());
}

TEST(v3_fs_open_handle_is_not_redirected_when_root_name_is_replaced)
{
    TempDir d("rename_race");
    TempDir victim("rename_victim");
    auto root = VaultRoot::create(d.path / "a.osv");
    REQUIRE(root.has_value());
    fs::rename(d.path / "a.osv", d.path / "moved.osv");
    std::error_code ec;
    fs::create_directory_symlink(victim.path, d.path / "a.osv", ec);
    REQUIRE(!ec);
    auto staged = root->create_staging_file();
    REQUIRE(staged.has_value());
    CHECK_TRUE(fs::is_empty(victim.path));
    CHECK_FALSE(fs::is_empty(d.path / "moved.osv/staging"));
}

TEST(v3_fs_faults_fail_closed_and_creation_rolls_back)
{
    TempDir d("fault");
    for (const auto point : {FsFault::Open, FsFault::Write, FsFault::FileSync, FsFault::Rename,
                             FsFault::DirectorySync, FsFault::Lock, FsFault::Unlink}) {
        clear_fs_faults();
        if (point == FsFault::Open) {
            inject_fs_fault(point);
            CHECK_FALSE(VaultRoot::create(d.path / "open.osv").has_value());
            CHECK_FALSE(fs::exists(d.path / "open.osv"));
            continue;
        }
        auto root =
            VaultRoot::create(d.path / ("x" + std::to_string(static_cast<int>(point)) + ".osv"));
        REQUIRE(root.has_value());
        if (point == FsFault::Lock) {
            inject_fs_fault(point);
            CHECK_FALSE(root->try_writer_lock().has_value());
        } else if (point == FsFault::Write || point == FsFault::FileSync) {
            auto f = root->create_staging_file();
            REQUIRE(f.has_value());
            inject_fs_fault(point);
            CHECK_FALSE(point == FsFault::Write ? f->write_all(std::array<uint8_t, 1>{7})
                                                : f->sync());
        } else if (point == FsFault::Rename || point == FsFault::DirectorySync) {
            auto f = root->create_staging_file();
            REQUIRE(f.has_value());
            REQUIRE(f->sync());
            inject_fs_fault(point);
            CHECK_FALSE(root->publish(*f, id_from(static_cast<uint8_t>(point))));
        } else {
            inject_fs_fault(point);
            CHECK_FALSE(root->unlink_staging("missing.tmp"));
        }
    }
    clear_fs_faults();
}

TEST(v3_fs_failed_create_never_removes_caller_directory)
{
    TempDir d("rollback");
    fs::create_directories(d.path / "existing.osv");
    {
        std::ofstream(d.path / "existing.osv/keep") << "mine";
    }
    CHECK_FALSE(VaultRoot::create(d.path / "existing.osv").has_value());
    CHECK_TRUE(fs::exists(d.path / "existing.osv/keep"));
}
