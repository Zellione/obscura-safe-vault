#include "test_framework.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "ui/import_queue.h"
#include "ui/zip_test_helpers.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

namespace {

// Helper: pump drain() until the queue idles
void pump_until_idle(ui::ImportQueue& q, int max_iterations = 2000)
{
    for (int i = 0; i < max_iterations; ++i) {
        (void)q.drain(0.016);
        if (!q.busy()) {
            (void)q.drain(0.016);
            return;
        }
    }
}

// Helper: check if a node with given name is a gallery
bool has_gallery_child(const std::vector<const vault::IndexNode*>& nodes, std::string_view name)
{
    return std::ranges::any_of(nodes, [name](const auto* node) {
        return node->name == name && node->is_gallery();
    });
}

// Helper: check if a node with given name is an image
bool has_image_child(const std::vector<const vault::IndexNode*>& nodes, std::string_view name)
{
    return std::ranges::any_of(nodes, [name](const auto* node) {
        return node->name == name && node->is_image();
    });
}

}  // namespace

// Test 1: folder_import_mirrors_subfolders_into_sub_galleries
TEST(folder_import_mirrors_subfolders_into_sub_galleries)
{
    const auto temp_dir = ziptest::fresh_dir("test_folder_import_subfolders");
    const auto vault_path = temp_dir / "vault.osv";

    vault::Vault v;
    ziptest::make_vault(v, vault_path);

    // Create a folder structure: root/a.jpg, root/sub/b.jpg
    const auto root = temp_dir / "import_root";
    fs::create_directories(root);
    fs::create_directories(root / "sub");

    const auto a_path = root / "a.jpg";
    const auto b_path = root / "sub" / "b.jpg";

    const auto jpeg_a = ziptest::fake_jpeg(1);
    const auto jpeg_b = ziptest::fake_jpeg(2);

    std::ofstream(a_path, std::ios::binary).write(reinterpret_cast<const char*>(jpeg_a.data()),
                                                  static_cast<std::streamsize>(jpeg_a.size()));
    std::ofstream(b_path, std::ios::binary).write(reinterpret_cast<const char*>(jpeg_b.data()),
                                                  static_cast<std::streamsize>(jpeg_b.size()));

    // Import
    ui::ImportQueue q;
    q.begin_session(v);
    (void)q.enqueue_folder(root, "", "Photos");

    // Pump until the queue drains (bounded so a hang fails rather than spins).
    pump_until_idle(q);
    q.abort_and_flush();

    // Fixture: root/a.jpg + root/sub/b.jpg
    // Expected: Photos/ contains a.jpg (image) + sub (gallery) = 2 items
    auto photos_children = v.list("Photos");
    CHECK_EQ(photos_children.size(), 2u);
    CHECK(has_image_child(photos_children, "a.jpg"));
    CHECK(has_gallery_child(photos_children, "sub"));

    // Expected: Photos/sub/ contains b.jpg (image) = 1 item
    auto sub_children = v.list("Photos/sub");
    CHECK_EQ(sub_children.size(), 1u);
    CHECK(has_image_child(sub_children, "b.jpg"));

    ziptest::cleanup_dir(temp_dir);
}

// Test 2: folder_import_survives_lock_and_reopen
TEST(folder_import_survives_lock_and_reopen)
{
    const auto temp_dir = ziptest::fresh_dir("test_folder_import_survives");
    const auto vault_path = temp_dir / "vault.osv";

    vault::Vault v;
    ziptest::make_vault(v, vault_path);

    // Create a folder structure
    const auto root = temp_dir / "import_root";
    fs::create_directories(root);
    fs::create_directories(root / "sub");

    const auto a_path = root / "a.jpg";
    const auto b_path = root / "sub" / "b.jpg";

    const auto jpeg_a = ziptest::fake_jpeg(1);
    const auto jpeg_b = ziptest::fake_jpeg(2);

    std::ofstream(a_path, std::ios::binary).write(reinterpret_cast<const char*>(jpeg_a.data()),
                                                  static_cast<std::streamsize>(jpeg_a.size()));
    std::ofstream(b_path, std::ios::binary).write(reinterpret_cast<const char*>(jpeg_b.data()),
                                                  static_cast<std::streamsize>(jpeg_b.size()));

    // Import
    ui::ImportQueue q;
    q.begin_session(v);
    (void)q.enqueue_folder(root, "", "Photos");
    pump_until_idle(q);
    q.end_session();

    // Lock and re-unlock
    v.lock();
    const std::vector<uint8_t> pw{'p', 'w'};
    const auto unlock_result = v.unlock(pw, {});
    CHECK_EQ(unlock_result, vault::VaultResult::Ok);

    // Check structure survived
    // Fixture: root/a.jpg + root/sub/b.jpg
    // Expected: Photos/ contains a.jpg (image) + sub (gallery) = 2 items
    auto photos_children = v.list("Photos");
    CHECK_EQ(photos_children.size(), 2u);
    CHECK(has_image_child(photos_children, "a.jpg"));
    CHECK(has_gallery_child(photos_children, "sub"));

    // Expected: Photos/sub/ contains b.jpg (image) = 1 item
    auto sub_children = v.list("Photos/sub");
    CHECK_EQ(sub_children.size(), 1u);
    CHECK(has_image_child(sub_children, "b.jpg"));

    ziptest::cleanup_dir(temp_dir);
}

// Test 3: folder_import_with_nested_galleries
TEST(folder_import_with_nested_galleries)
{
    const auto temp_dir = ziptest::fresh_dir("test_folder_import_nested");
    const auto vault_path = temp_dir / "vault.osv";

    vault::Vault v;
    ziptest::make_vault(v, vault_path);

    // Create nested structure: root/a.jpg, root/sub1/b.jpg, root/sub1/sub2/c.jpg
    const auto root = temp_dir / "import_root";
    fs::create_directories(root);
    fs::create_directories(root / "sub1");
    fs::create_directories(root / "sub1" / "sub2");

    const auto a_path = root / "a.jpg";
    const auto b_path = root / "sub1" / "b.jpg";
    const auto c_path = root / "sub1" / "sub2" / "c.jpg";

    const auto jpeg_a = ziptest::fake_jpeg(1);
    const auto jpeg_b = ziptest::fake_jpeg(2);
    const auto jpeg_c = ziptest::fake_jpeg(3);

    std::ofstream(a_path, std::ios::binary).write(reinterpret_cast<const char*>(jpeg_a.data()),
                                                  static_cast<std::streamsize>(jpeg_a.size()));
    std::ofstream(b_path, std::ios::binary).write(reinterpret_cast<const char*>(jpeg_b.data()),
                                                  static_cast<std::streamsize>(jpeg_b.size()));
    std::ofstream(c_path, std::ios::binary).write(reinterpret_cast<const char*>(jpeg_c.data()),
                                                  static_cast<std::streamsize>(jpeg_c.size()));

    // Import
    ui::ImportQueue q;
    q.begin_session(v);
    (void)q.enqueue_folder(root, "", "Photos");
    pump_until_idle(q);
    q.end_session();

    // Check nested structure
    // Fixture: root/a.jpg + root/sub1/b.jpg + root/sub1/sub2/c.jpg
    // Expected: Photos/ contains a.jpg (image) + sub1 (gallery) = 2 items
    auto photos_children = v.list("Photos");
    CHECK_EQ(photos_children.size(), 2u);
    CHECK(has_image_child(photos_children, "a.jpg"));
    CHECK(has_gallery_child(photos_children, "sub1"));

    // Expected: Photos/sub1/ contains b.jpg (image) + sub2 (gallery) = 2 items
    auto sub1_children = v.list("Photos/sub1");
    CHECK_EQ(sub1_children.size(), 2u);
    CHECK(has_image_child(sub1_children, "b.jpg"));
    CHECK(has_gallery_child(sub1_children, "sub2"));

    // Expected: Photos/sub1/sub2/ contains c.jpg (image) = 1 item
    auto sub2_children = v.list("Photos/sub1/sub2");
    CHECK_EQ(sub2_children.size(), 1u);
    CHECK(has_image_child(sub2_children, "c.jpg"));

    ziptest::cleanup_dir(temp_dir);
}
