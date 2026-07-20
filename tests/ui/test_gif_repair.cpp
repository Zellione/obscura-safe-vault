#include "test_framework.h"

#include "ui/gif_repair.h"
#include "ui/zip_test_helpers.h"
#include "vault/vault.h"

#include <fstream>
#include <vector>

// Tests for ui::maybe_repair_gif_animated — lazy self-healing for legacy GIFs
// stored before Phase 47 with the wrong animated flag.

namespace {

std::vector<uint8_t> load_vault_gif_fixture(const char* name)
{
#ifndef OSV_VAULT_FIXTURE_DIR
#define OSV_VAULT_FIXTURE_DIR "tests/vault/fixtures"
#endif
    const std::string path = std::string(OSV_VAULT_FIXTURE_DIR) + "/" + name;
    std::ifstream     f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

std::vector<uint8_t> load_jpeg_fixture()
{
    // A minimal valid JPEG: FFD8 FFE0 0010 4A46 4946 0001 0100 0001 0001 0000 FFDB ... FFD9
    return {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01,
            0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x08,
            0x06, 0x06, 0x07, 0x06, 0x05, 0x08, 0x07, 0x07, 0x07, 0x09, 0x09, 0x08, 0x0A,
            0x0C, 0x14, 0x0D, 0x0C, 0x0B, 0x0B, 0x0C, 0x19, 0x12, 0x13, 0x0F, 0x14, 0x1D,
            0x1A, 0x1F, 0x1E, 0x1D, 0x1A, 0x1C, 0x1C, 0x20, 0x24, 0x2E, 0x27, 0x20, 0x22,
            0x2C, 0x23, 0x1C, 0x1C, 0x28, 0x37, 0x29, 0x2C, 0x30, 0x31, 0x34, 0x34, 0x34,
            0x1F, 0x27, 0x39, 0x3D, 0x38, 0x32, 0x3C, 0x2E, 0x33, 0x34, 0x32, 0xFF, 0xC0,
            0x00, 0x0B, 0x08, 0x00, 0x01, 0x00, 0x01, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xC4,
            0x00, 0x1F, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
            0x07, 0x08, 0x09, 0x0A, 0x0B, 0xFF, 0xC4, 0x00, 0xB5, 0x10, 0x00, 0x02, 0x01,
            0x03, 0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7D,
            0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13,
            0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42,
            0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A,
            0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35,
            0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A,
            0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67,
            0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84,
            0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
            0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3,
            0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
            0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1,
            0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4,
            0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00,
            0x00, 0x3F, 0x00, 0xFB, 0xD0, 0xFF, 0xD9};
}

}  // namespace

using ziptest::cleanup_dir;
using ziptest::fresh_dir;
using ziptest::make_vault;

TEST(gif_repair_sets_the_flag_on_a_legacy_animated_gif)
{
    auto anim_bytes = load_vault_gif_fixture("anim.gif");
    REQUIRE(!anim_bytes.empty());

    auto dir = fresh_dir("osv_gif_repair_legacy_animated");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(v.add_image("", anim_bytes, "anim.gif") == vault::VaultResult::Ok);

        auto children = v.list("");
        REQUIRE(children.size() == 1);
        const vault::IndexNode* node = children[0];
        REQUIRE(node->is_image());
        REQUIRE(node->name == "anim.gif");

        // Simulate legacy vault by clearing the flag
        REQUIRE(v.repair_image_animated("anim.gif", false) == true);

        // Verify flag is cleared
        auto children2 = v.list("");
        REQUIRE(children2.size() == 1);
        REQUIRE(!children2[0]->meta.animated);

        // Repair should detect that it's animated and fix it
        CHECK(ui::maybe_repair_gif_animated(v, "", *children2[0], anim_bytes));

        // Verify flag is now set
        auto children3 = v.list("");
        REQUIRE(children3.size() == 1);
        CHECK(children3[0]->meta.animated);
    }
    cleanup_dir(dir);
}

TEST(gif_repair_is_a_no_op_when_the_flag_is_already_correct)
{
    auto anim_bytes = load_vault_gif_fixture("anim.gif");
    REQUIRE(!anim_bytes.empty());

    auto dir = fresh_dir("osv_gif_repair_noop");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(v.add_image("", anim_bytes, "anim.gif") == vault::VaultResult::Ok);

        auto children = v.list("");
        REQUIRE(children.size() == 1);
        REQUIRE(children[0]->meta.animated);

        // Repair should be a no-op since the flag is already correct
        CHECK(!ui::maybe_repair_gif_animated(v, "", *children[0], anim_bytes));

        auto children2 = v.list("");
        REQUIRE(children2.size() == 1);
        CHECK(children2[0]->meta.animated);
    }
    cleanup_dir(dir);
}

TEST(gif_repair_clears_a_wrongly_set_flag_on_a_still_gif)
{
    auto still_bytes = load_vault_gif_fixture("still.gif");
    REQUIRE(!still_bytes.empty());

    auto dir = fresh_dir("osv_gif_repair_still_gif");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(v.add_image("", still_bytes, "still.gif") == vault::VaultResult::Ok);

        auto children = v.list("");
        REQUIRE(children.size() == 1);
        REQUIRE(!children[0]->meta.animated);

        // Force the animated flag on (simulate corruption)
        REQUIRE(v.repair_image_animated("still.gif", true) == true);

        auto children2 = v.list("");
        REQUIRE(children2.size() == 1);
        REQUIRE(children2[0]->meta.animated);

        // Repair should detect it's not animated and fix it
        CHECK(ui::maybe_repair_gif_animated(v, "", *children2[0], still_bytes));

        auto children3 = v.list("");
        REQUIRE(children3.size() == 1);
        CHECK(!children3[0]->meta.animated);
    }
    cleanup_dir(dir);
}

TEST(gif_repair_ignores_non_gif_images)
{
    auto jpeg_bytes = load_jpeg_fixture();
    REQUIRE(!jpeg_bytes.empty());

    auto dir = fresh_dir("osv_gif_repair_ignores_jpeg");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(v.add_image("", jpeg_bytes, "pic.jpg") == vault::VaultResult::Ok);

        auto children = v.list("");
        REQUIRE(children.size() == 1);
        REQUIRE(children[0]->meta.format != vault::ImageFormat::GIF);

        // Repair should be a no-op for non-GIF images
        CHECK(!ui::maybe_repair_gif_animated(v, "", *children[0], jpeg_bytes));
    }
    cleanup_dir(dir);
}

TEST(gif_repair_survives_a_reopen)
{
    auto anim_bytes = load_vault_gif_fixture("anim.gif");
    REQUIRE(!anim_bytes.empty());

    auto dir = fresh_dir("osv_gif_repair_persist");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(v.add_image("", anim_bytes, "anim.gif") == vault::VaultResult::Ok);

        // Simulate legacy vault by clearing the flag
        REQUIRE(v.repair_image_animated("anim.gif", false) == true);

        auto children = v.list("");
        REQUIRE(children.size() == 1);
        REQUIRE(!children[0]->meta.animated);

        // Repair it
        CHECK(ui::maybe_repair_gif_animated(v, "", *children[0], anim_bytes));

        auto children2 = v.list("");
        REQUIRE(children2.size() == 1);
        CHECK(children2[0]->meta.animated);

        v.lock();
    }

    // Reopen the vault and verify the flag persisted
    {
        vault::Vault v;
        REQUIRE(vault::Vault::open(dir / "v.osv", v) == vault::VaultResult::Ok);
        const std::vector<uint8_t> pw{'p', 'w'};
        REQUIRE(v.unlock(pw, {}) == vault::VaultResult::Ok);

        auto children = v.list("");
        REQUIRE(children.size() == 1);
        CHECK(children[0]->meta.animated);
    }
    cleanup_dir(dir);
}
